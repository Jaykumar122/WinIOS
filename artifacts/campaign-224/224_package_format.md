# WinIOS Campaign 224 — Stage 224.3 WIOSPKG2 Package Format & Validation

## 1. Container Specification
The WIOSPKG2 package container is a hardened binary distribution format designed for ARM64 Darwin-targeted guest applications on the WinIOS research platform.

### Binary Header Layout
| Field | Offset (bytes) | Type | Value / Description |
| :--- | :---: | :---: | :--- |
| Magic | `0x00 - 0x07` | `char[8]` | `"WIOSPKG2"` (Legacy v1: `"WIOSPKG1"`) |
| Manifest Length (`mlen`) | `0x08 - 0x0B` | `uint32_t` | Byte length of serialized manifest (bounded: $1 \le \text{mlen} \le 65536$) |
| Manifest Payload | `0x0C - (0x0C + mlen - 1)` | `char[mlen]` | UTF-8 encoded key-value manifest string |
| File Count (`file_count`) | Offset after manifest | `uint32_t` | Total number of embedded files ($1 \le \text{count} \le 256$) |

### File Entry Structure
For each file in `0 .. file_count - 1`:
| Field | Type | Bounds / Validation |
| :--- | :---: | :--- |
| Path Length (`plen`) | `uint16_t` | $1 \le \text{plen} \le 256$ |
| Path String | `char[plen]` | UTF-8 relative path without leading slash, colon, or `..` |
| Data Size (`dsize`) | `uint32_t` | Bounded: $\text{dsize} \le 32\,\text{MB}$ |
| Data Payload | `uint8_t[dsize]` | File content bytes, SHA-256 verified against manifest |

---

## 2. Hardened Validation Rules
1. **Magic Verification:** Rejects payloads not starting with `"WIOSPKG1"` or `"WIOSPKG2"`.
2. **Buffer Bounds & Integer Overflow:** All offset advances (`off + len > size`) use checked 64-bit bounds checks, preventing wrap-around exploits.
3. **Manifest Size Ceiling:** Manifest length is capped at 64 KB (`kMaxManifestLen = 65536`) to prevent memory exhaustion.
4. **File Count Ceiling:** Number of embedded files cannot exceed 256 (`kMaxFileCount = 256`).
5. **Path Traversal Defense:** Every file path is inspected:
   - Must not contain `..`
   - Must not start with `/` or `\`
   - Must not contain `:`
6. **Duplicate File Rejection:** A package containing duplicate relative file paths is rejected with `PackageValidationResult::DuplicateFileEntry`.
7. **Architecture Conformance:** Manifest architecture field must match `"arm64"` or `"aarch64"` if specified.
8. **Executable Verification:** The binary specified by `executable_name` must be present in the package files and have a non-zero byte size.
