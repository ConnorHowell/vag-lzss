# LZSS Compression Tool (VAG Variant)

VAG specific LZSS compression/decompression tool reverse-engineered and modified to produce byte-perfect output matching the original OEM tooling used.

## Attribution

- **Original Implementation**: Michael Dipperstein - LZSS encoding/decoding routines
- **Simos18 Adaptation**: [bri3d/VW_Flash](https://github.com/bri3d/VW_Flash) - Modified for Simos18 ECU compatibility
- **VAG Universal Modifications**: Connor Howell, 2025

The latest modifications extend compatibility beyond Simos18 to match output from the original OEM tooling across all other ECUs which use LZSS.

## Overview

This is not a standard LZSS implementation. It replicates the exact compression behavior of the VAG OEM tooling, which uses several non-standard design choices that produce different output than typical LZSS implementations.

## Technical Specification

### Parameters (Dictionary Size 1023)

| Parameter | Value |
|-----------|-------|
| Window Size | 1023 bytes (10-bit offset) |
| Length Bits | 6 bits |
| Max Match Length | 63 bytes |
| Min Match Length | 3 bytes |
| Token Size | 16 bits (2 bytes) |
| Flags per Byte | 8 tokens |

### Stream Format

```
[FLAGS] [TOKEN_0] [TOKEN_1] ... [TOKEN_7] [FLAGS] [TOKEN_8] ...
```

- **Flag bit = 0**: Literal byte (1 byte)
- **Flag bit = 1**: Match token (2 bytes)

### Match Token Encoding

```
BYTE_1: [LENGTH:6][OFFSET_HI:2]
BYTE_2: [OFFSET_LO:8]

Decode:
  offset = ((BYTE_1 & 0x03) << 8) | BYTE_2
  length = BYTE_1 >> 2
```

## Key Differences from Standard LZSS

| Aspect | Standard LZSS | This Implementation |
|--------|---------------|---------------------|
| **Search Direction** | Typically searches backward from current position | Searches from offset 3 upward (increasing distance) |
| **Flag Bit Order** | Varies | MSB first (bit 7 = first token) |
| **Match Selection** | Usually longest match | Greedy: first match reaching max_length |
| **Bit Allocation** | Fixed | Variable based on dictionary size |
| **Ring Buffer Init** | Often pre-filled with spaces/zeros | Starts empty (position 0) |
| **End Marker** | Common to include | None - relies on known decompressed size |
| **Minimum Offset** | Usually 1 | Must be >= 3 |

## Usage

```bash
# Compress
lzss -c -i input.bin -o output.lzss

# Decompress
lzss -d -i input.lzss -o output.bin

# Compress with exact-length padding (for bootloaders expecting precise sizes)
lzss -c -e -i input.bin -o output.lzss

# Compress without 16-byte alignment padding
lzss -c -p -i input.bin -o output.lzss

# Use stdin/stdout
lzss -c -s < input.bin > output.lzss
```

### Options

| Flag | Description |
|------|-------------|
| `-c` | Compress mode |
| `-d` | Decompress mode |
| `-e` | Exact padding mode (padding bytes decode as no-op) |
| `-p` | Disable 16-byte alignment padding |
| `-s` | Use stdin/stdout |
| `-i <file>` | Input file |
| `-o <file>` | Output file |

## Building

```bash
gcc -O2 -o lzss lzss.c
```

Windows:
```bash
gcc -O2 -o lzss.exe lzss.c
```

## License

LGPL v2.1
