# Strawberry Cheesecake

Strawberry cheesecake is a data compression utility specifically designed for compressing numeric
data.

This project stemmed out of a need for a better solution for compressing digits of pi. I had about
400 billion digits of pi, which took gzip 12 hours to compress (at a fairly lame compression ratio).

This utility is very special case and for its special case, strawberry cheesecake is both faster and
more effective than any other file compression utility (at least of the ones I benchmarked against).

Algorithm | Compression Ratio | Time
-- | -- | --
`bzip2 -1` | 0.431 | 72m 58s
`bzip2 -9` | 0.431 | 81m 0s
`gzip -1` | 0.497 | 23m 21s
`gzip -9` | 0.469 | 74m 59s
`xz -0` | 0.486 | 82m 55s
`xz -9` | 0.441 | 1475m 58s
`plzip -0` | 0.470 | 15m 45s
`plzip -9` | 0.441 | 256m 55s
`strawberrycheesecake` | **0.425** | **13m 26s**

Benchmark used 50 billion digits of pi on an i7 and an hdd.

Strawberry cheesecake uses huffman coding with a fixed huffman tree. The fixed huffman tree treats
each digit as equiprobable, as the digits of pi are equiprobable. Using a fixed huffman tree allows
the compression to be done in one go instead of reading imput once to generate the tree then reading
a second time to actually encode the data. Lookups tables are used to allow the coding and decoding
processes to be done very efficiently. Strawberry cheesecake archives include a CRC64 in their file
headers to help verify data integrity.

## Usage

```bash
# Compile:
gcc main.c -O3 -funroll-loops -o strawberrycheesecake
# Compress:
./strawberrycheesecake input.txt output.sc
# Extract:
./strawberrycheesecake -x output.sc data.txt
```
