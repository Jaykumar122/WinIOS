// Raw little-endian AArch64 words used by test_program.bin.
// Verified against the existing interpreter's instruction encodings.
// MOVZ X0, #42
// ADD X0, X0, #5
// MOVZ X1, #0x100
// STR X0, [X1, #0]
// B .
.word 0xD2800540
.word 0x91001400
.word 0xD2802001
.word 0xF9000020
.word 0x14000000
