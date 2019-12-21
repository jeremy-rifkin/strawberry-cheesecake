#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#define false 0
#define true 1
#define bool int
#define null NULL

#ifdef _WIN64
// Make editor happy while developing on windows
#define POSIX_FADV_SEQUENTIAL 0
#define size_t int
#pragma message ("No.") // Don't actually allow this to compile
#endif

// CRC64 from http://www0.cs.ucl.ac.uk/staff/d.jones/crcnote.pdf
#define POLY64REV  0x95AC9329AC4BC9B5ULL
#define CRC64_INIT 0xFFFFFFFFFFFFFFFFULL
uint64_t CRCTable[256];

typedef struct code {
	char ascii;
	char value;
	int bits;
} code;

void pbin(char n) { // Debug util
	int q = 0x100;
	while(q >>= 1)
		printf(n & q ? "1" : "0");
}
void printUint64(uint64_t v) { // Debug util
	uint64_t m = 0xff00000000000000ULL;
	int s = 7;
	do {
		printf("%02lX", (v & m) >> (s * 8));
		m >>= 8;
	} while(s--);
}

// ascii to code lookup table
code codesAscii[128];
// code to ascii lookup table
code codesBin[0x100];

void initTables() {
	// TODO: Code lookup tables use pointers? Handled as pointers later anyway
	// Setup the ascii -> code table
	codesAscii['0'] = (code) { .ascii = '0', .value = 0x0, .bits = 3 };
	codesAscii['1'] = (code) { .ascii = '1', .value = 0x1, .bits = 3 };
	codesAscii['2'] = (code) { .ascii = '2', .value = 0x2, .bits = 3 };
	codesAscii['3'] = (code) { .ascii = '3', .value = 0x3, .bits = 3 };
	codesAscii['4'] = (code) { .ascii = '4', .value = 0x4, .bits = 3 };
	codesAscii['5'] = (code) { .ascii = '5', .value = 0x5, .bits = 3 };
	codesAscii['6'] = (code) { .ascii = '6', .value = 0xC, .bits = 4 };
	codesAscii['7'] = (code) { .ascii = '7', .value = 0xD, .bits = 4 };
	codesAscii['8'] = (code) { .ascii = '8', .value = 0xE, .bits = 4 };
	codesAscii['9'] = (code) { .ascii = '9', .value = 0xF, .bits = 4 };
	// Set up the code -> ascii table
	for(int i = '0'; i <= '9'; i++) {
		code* c = &codesAscii[i];
		unsigned char byte = c->value << (8 - c->bits);
		for(int i = 0; i < (1 << (8 - c->bits)); i++) {
			codesBin[byte] = *c;
			byte++;
		}
	}
	// Setup the CRC table
	uint64_t t;
	for(int i = 0; i < 256; i++) {
	    t = i;
	    for(int j = 0; j < 8; j++) {
			if(t & 1)
				t = (t >> 1) ^ POLY64REV;
			else
				t >>= 1;
	    }
	    CRCTable[i] = t;
	}
}

#define MAGIC_0 'S'
#define MAGIC_1 'C'
#define FILEFORMAT_VER 0x0
#define HEADER_SIZE 19

void help() {
	printf("Usage:");
	printf("strawberrycheesecake [options] input output\n");
	printf("Options:\n");
	printf("      -c   Compress input and save as output.\n");
	printf("      -x   Extract input and save as output.\n");
	printf("      -p   Turns on pi mode - skips \"3.\" in file and adds it back later.\n");
	printf("      -h   Display help.\n");
}

#define BUFFER_SIZE 32768

// Safe guard...
#if HEADER_SIZE > BUFFER_SIZE
#error No.
#endif

static inline void writebuf(char* buffer, int dest, int count) {
	int written = write(dest, buffer, count);
	if(written == -1) {
		fprintf(stderr, "[Error] Output failed.\n");
		exit(1);
	} else if(written != count) {
		fprintf(stderr, "[Error] Output failed - insufficient space in dest.\n");
		exit(1);
	}
}
static inline void readfixed(int src, char* buffer, int count) {
	size_t bytesRead;
	if((bytesRead = read(src, buffer, count)) != count) {
		if(bytesRead == -1) {
			fprintf(stderr, "[Error] Error occurred while reading input.\n");
			exit(1);
		} else if(bytesRead == 0) {
			fprintf(stderr, "[Error] Error occurred while reading input - no input available.\n");
			exit(1);
		} else if(bytesRead < count) {
			fprintf(stderr, "[Error] Error occurred while reading input - didn't get requested "
								"number of bytes.\n");
			exit(1);
		} else {
			fprintf(stderr, "[Error] Error that can't happen.\n");
			exit(1);
		}
	}
}
void compress(int src, int dest, bool pi) {
	// write header after? Also a way of indicating file is done writing?
	posix_fadvise(src, 0, 0, POSIX_FADV_SEQUENTIAL);  // FDADVICE_SEQUENTIAL
	// IO buffers
	char ibuf[BUFFER_SIZE];
	char obuf[BUFFER_SIZE];
	// Current byte assembly
	char byte = 0x0;
	int bi = 0; // How many bits we've filled
	// Info on the byte we're encoding
	code* c;
	// Output buffer index
	int oi = HEADER_SIZE; // Skip header - come back later
	// CRC
	uint64_t crc = CRC64_INIT;
	// Data length
	uint64_t len = 0;
	// Read src
	size_t bytesRead;
	// If in pi mode...
	if(pi) {
		readfixed(src, ibuf, 2);
		if(!(ibuf[0] == '3' && ibuf[1] == '.')) {
			fprintf(stderr, "[Error] Pi mode active yet data does not start with \"3.\".\n");
			exit(1);
		}
	}
	// Move onto source
	while((bytesRead = read(src, ibuf, BUFFER_SIZE)) > 0) {
		// Book keeping
		len += bytesRead;
		for(int i = 0; i < bytesRead; i++) {
			// TODO handle values outside of '0' - '9' or just trust input?
			// Handle crc
			crc = CRCTable[(crc ^ ibuf[i]) & 0xff] ^ (crc >> 8);
			// Handle code
			c = &codesAscii[ibuf[i]];
			// Break into 3 cases
			if(c->bits + bi == 8) { // code fits perfectly
				byte = (byte << c->bits) | c->value;
				obuf[oi++] = byte;
				if(oi == BUFFER_SIZE) {
					writebuf(obuf, dest, oi);
					oi = 0;
				}
				bi = 0;
			} else if(c->bits + bi < 8) { // code falls short of fitting
				byte = (byte << c->bits) | c->value;
				bi += c->bits;
			} else { // code overflows byte
				byte = (byte << (8 - bi)) | (c->value >> (c->bits - (8 - bi)));
				obuf[oi++] = byte;
				if(oi == BUFFER_SIZE) {
					writebuf(obuf, dest, oi);
					oi = 0;
				}
				byte = c->value & ((1 << (c->bits - (8 - bi))) - 1);
				bi = c->bits - (8 - bi);
			}
		}
	}
	// Check for read errors
	if(bytesRead == -1) {
		fprintf(stderr, "[Error] Error occurred while reading input.\n");
		exit(1);
	}
	// Handle residual byte
	if(bi > 0)
		obuf[oi++] = byte << (8 - bi);
	// Print what's left in buffer
	if(oi > 0)
		writebuf(obuf, dest, oi);
	// Go back and write file header
	if(lseek(dest, 0, SEEK_SET) == -1) {
		fprintf(stderr, "[Error] Error occurred while seeking output.\n");
		exit(1);
	}
	oi = 0;
	obuf[oi++] = MAGIC_0;
	obuf[oi++] = MAGIC_1;
	obuf[oi++] = FILEFORMAT_VER; // mainly for futureproofing
	// Write length in little endian
	for(int i = 0; i < 8; i++) {
		obuf[oi++] = len & 0xff;
		len >>= 8;
	}
	// Write crc64 in little endian
	for(int i = 0; i < 8; i++) {
		obuf[oi++] = crc & 0xff;
		crc >>= 8;
	}
	// Write header
	writebuf(obuf, dest, oi); // oi == HEADER_SIZE
}
void extract(int src, int dest, bool pi) {
	posix_fadvise(src, 0, 0, POSIX_FADV_SEQUENTIAL);  // FDADVICE_SEQUENTIAL
	// IO buffers
	unsigned char ibuf[BUFFER_SIZE];
	char obuf[BUFFER_SIZE];
	// Current byte assembly
	unsigned char byte = 0x0;
	int bi = 0;
	// Info on the byte we're encoding
	code* c;
	// Output buffer index
	int oi = 0;
	// CRC
	uint64_t crc = CRC64_INIT;
	// Read src
	size_t bytesRead;
	// Handle header
	readfixed(src, ibuf, HEADER_SIZE);
	if(!(ibuf[0] == MAGIC_0 && ibuf[1] == MAGIC_1)) {
		fprintf(stderr, "[Error] File does not appear to by a strawberrycheesecake archive.\n");
		exit(1);
	}
	if(ibuf[2] != FILEFORMAT_VER) { // mainly for futureproofing
		fprintf(stderr, "[Error] Archive reports unsupported file version.\n");
		exit(1);
	}
	uint64_t srclen = 0;
	for(int i = 0; i < 8; i++)
		srclen |= (uint64_t)ibuf[3 + i] << (i * 8);
	uint64_t original_crc = 0;
	for(int i = 0; i < 8; i++)
		original_crc |= (uint64_t)ibuf[11 + i] << (i * 8);
	// If in pi mode.....
	if(pi) {
		obuf[oi++] = '3';
		obuf[oi++] = '.';
	}
	// Move onto data
	while((bytesRead = read(src, ibuf, BUFFER_SIZE)) > 0) {
		for(int i = 0; i < bytesRead; i++) {
			byte |= ibuf[i] >> bi;
			// Get two codes from the byte. There have to be 2 codes in a byte.
			int nbi = 8;
			for(int j = 0; j < 2; j++) {
				// Handle data length
				if(srclen-- == 0)
					goto end;
				// Code extraction
				c = &codesBin[byte];
				obuf[oi++] = c->ascii;
				byte <<= c->bits;
				nbi -= c->bits;
				// Handle buffer
				if(oi == BUFFER_SIZE) {
					writebuf(obuf, dest, oi);
					oi = 0;
				}
				// Handle crc
				crc = CRCTable[(crc ^ c->ascii) & 0xff] ^ (crc >> 8);
			}
			// Setup byte
			byte |= (ibuf[i] & ((1 << bi) - 1)) << (8 - bi - nbi);
			bi += nbi;
			// Handle residual byte - there can be at most 1 residual byte
			if(bi >= 3) {
				c = &codesBin[byte];
				if(c->bits <= bi) {
					// Basically a repeat of the code in the above for loop
					// Handle data length
					if(srclen-- == 0)
						goto end;
					// Code extraction
					obuf[oi++] = c->ascii;
					byte <<= c->bits;
					bi -= c->bits;
					// Handle buffer
					if(oi == BUFFER_SIZE) {
						writebuf(obuf, dest, oi);
						oi = 0;
					}
					// Handle crc
					crc = CRCTable[(crc ^ c->ascii) & 0xff] ^ (crc >> 8);
				}
			}
		}
	}
	end: // Break out of loop
	// Check for read errors
	if(bytesRead == -1) {
		fprintf(stderr, "[Error] Error occurred while reading input.\n");
		exit(1);
	}
	// Final write
	if(oi > 0)
		writebuf(obuf, dest, oi);
	// Check CRC
	if(crc != original_crc) {
		fprintf(stderr, "[Warning] CRC64 mismatch.\n");
		printf("CRC64:    "); printUint64(crc); printf("\n");
		printf("Original: "); printUint64(original_crc); printf("\n");
	} else
		printf("CRC64 matched\n");
}

int main(int argc, char* argv[]) {
	if(argc < 2) {
		help();
		return 0;
	}
	// Lookup table
	initTables();
	// Parameters
	bool compress_mode = false;
	bool extract_mode = false;
	//bool fast = false;
	bool pi = false;
	char* input = null;
	char* output = null;
	// Process arguments
	enum state { p_input, p_output, p_done };
	enum state currentState = p_input;
	for(int i = 1; i < argc; i++) {
		if(argv[i][0] == '-') {
			for(int j = 0; argv[i][++j] != 0x0; )
				switch(argv[i][j]) {
					case 'c':
						compress_mode = true;
						break;
					case 'x':
						extract_mode = true;
						break;
					//case 'f':
						//fast = true;
						//break;
					case 'p':
						pi = true;
						break;
					case 'h':
						help();
						break;
					default:
						fprintf(stderr, "[Warning] Unknown option %c\n", argv[i][j]);
				}
		} else {
			switch(currentState) {
				case p_input:
					input = argv[i];
					currentState = p_output;
					break;
				case p_output:
					output = argv[i];
					currentState = p_done;
					break;
				case p_done:
					fprintf(stderr, "[Warning] Unexpected positional argument \"%s\".\n", argv[i]);
					break;
			}
		}
	}
	// Compress default
	if(!extract_mode && !compress_mode)
		compress_mode = true;
	// Handle paradox
	if(extract_mode && compress_mode) {
		fprintf(stderr, "[Error] Cannot compress and extract at the same time.\n");
		return 1;
	}
	// Check that file was specified
	if(input == null) {
		fprintf(stderr, "[Error] No input specified.\n");
		return 1;
	}
	// Check that input exists
	if(access(input, F_OK) == -1) {
		fprintf(stderr, "[Error] Input file does not exist.\n");
		return 1;
	}
	// Check input perms
	if(access(input, R_OK) == -1) {
		fprintf(stderr, "[Error] User does not have permissions to read input.\n");
		return 1;
	}

	// TODO: pipe support?
	// isatty(fileno(stdin))
	// https://stackoverflow.com/questions/9084099/re-opening-stdout-and-stdin-file-descriptors-after-closing-them
	// https://stackoverflow.com/questions/1312922/detect-if-stdin-is-a-terminal-or-pipe
	int input_fd;
	int output_fd;
	printf("in: %s\n", input);
	printf("output: %s\n", output);
	input_fd = open(input, O_RDONLY);
	if(input_fd == -1) {
		fprintf(stderr, "[Error] Failed to open input file.\n"); // TODO More error info
		return 1;
	}

	// TODO output checking
	if(output == null) {
		fprintf(stderr, "[Error] No output.\n");
		return 1;
	} else {
		// For now just trust output
		output_fd = open(output, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
		if(output_fd == -1) {
			fprintf(stderr, "[Error] Failed to open output file.\n"); // TODO More error info
			return 1;
		}
	}
	
	// TODO: Improve error messages?
	// Done with checks, move onto program
	if(compress_mode) {
		printf("Compressing\n");
		compress(input_fd, output_fd, pi);
	} else if(extract_mode) {
		printf("Extracting\n");
		extract(input_fd, output_fd, pi);
	}

	close(input_fd);
	close(output_fd);

	return 0;
}

// TODO: More info on errors
