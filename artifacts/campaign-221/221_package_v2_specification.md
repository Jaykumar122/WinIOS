# WinIOS Package Format v2 Specification

**Campaign:** 221 (Stage 221.16)  
**Specification Version:** 2.0  
**Container Extension:** `.wios`  
**Container Magic:** `WIOSPKG2` (8 bytes)  

---

## 1. Binary Container Layout

The `.wios` format v2 is a single-file binary package designed for atomic distribution and verified installation:

```
[Offset 0x00]: Magic "WIOSPKG2" (8 bytes)
[Offset 0x08]: Manifest Length `mlen` (uint32_t, 4 bytes, little-endian)
[Offset 0x0C]: Manifest UTF-8 Text Payload (`mlen` bytes)
[Offset 0x0C + mlen]: File Count `N` (uint32_t, 4 bytes, little-endian)
[File Entry 0]:
    - Path Length `plen` (uint16_t, 2 bytes, little-endian)
    - Relative Path String (`plen` bytes, e.g. "bin/calc")
    - Data Size `dsize` (uint32_t, 4 bytes, little-endian)
    - Data Payload (`dsize` bytes)
...
[File Entry N-1]
```

---

## 2. Manifest Schema (Key-Value Format)

```ini
format_version=2.0
bundle_id=com.winios.runtime-explorer
name=Runtime Explorer
version=1.0.0
min_os_version=221.0
architecture=arm64
min_abi_version=1.5
executable_name=bin/runtime_explorer
storage_quota_bytes=10485760
signature_sha256=d3b07384d113edec49eaa6238ad5ff00
permissions=display.window,storage.private,logging
```

---

## 3. Backward Compatibility

`AppPackage::unpack()` detects both `WIOSPKG1` (v1 legacy) and `WIOSPKG2` (v2 enhanced), automatically providing defaults for optional v2 metadata fields when unpacking older packages.
