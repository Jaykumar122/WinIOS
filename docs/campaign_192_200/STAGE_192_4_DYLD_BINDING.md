# WINIOS CAMPAIGN 192 — STAGE 192.4 REPORT
## Dyld Classic Rebase & Binding Hardening and Conformance

**Campaign:** 192  
**Stage:** 192.4 (Dyld Classic Rebase & Binding Hardening)  
**Date:** 2026-11-13  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host Architecture:** Windows 11 x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Verdict:** **PASSED (8/8 test suites passed, 100% CTest regression clean, differential match confirmed)**

---

## 1. Executive Summary

In Stage 192.4, the classic Darwin dynamic linking bytecode engines for rebasing and binding (`LC_DYLD_INFO_ONLY` / `LC_DYLD_INFO`) were audited, hardened, and brought into complete parity between the host loader architecture (`winios::loader::DyldEngine` in `src/loader/dyld_engine.cpp` and `src/loader/dyld_engine.h`) and the guest dynamic linker.

Key accomplishments achieved in Stage 192.4:
1. **Classic Rebase Bytecode Engine:** Implemented `DyldEngine::parse_rebase_stream` to parse all Mach-O rebase opcodes (`SET_TYPE_IMM`, `SET_SEGMENT_AND_OFFSET_ULEB`, `ADD_ADDR_ULEB`, `ADD_ADDR_IMM_SCALED`, `DO_REBASE_IMM_TIMES`, `DO_REBASE_ULEB_TIMES`, `DO_REBASE_ADD_ADDR_ULEB`, `DO_REBASE_ULEB_TIMES_SKIPPING_ULEB`, and `DONE`).
2. **Safe ULEB128 & SLEB128 Parsers:** Created `read_uleb128_safe` and `read_sleb128_safe` to detect truncated bytecode streams, prevent undefined 64-bit integer bit shifts (`shift > 63`), and reject malformed variable-length encodings before they cause parser stalls or out-of-bounds reads.
3. **Out-of-Bounds Segment Index Guards:** Enforced strict verification for segment indices (`seg_index < segment_vmaddrs.size()`) across both rebase and binding operations, eliminating silent mapping failures.
4. **Malformed Stream & Opcode Rejection:** Hardened opcode decoders to cleanly fail with descriptive error messages upon encountering unknown opcodes or unterminated symbol name strings.
5. **Weak Import Resolution:** Verified and hardened `BIND_SYMBOL_FLAGS_WEAK_IMPORT` semantics: missing weak symbols cleanly resolve to NULL (`0ULL`) without halting process execution, while missing strong symbols trigger `DyldLinkStatus::UndefinedSymbol`.
6. **Signed SLEB128 Addend Support:** Validated negative and positive signed addends in dynamic pointer binding.

---

## 2. Hardening Audit Findings & Implemented Protections

### 2.1 Classic Rebase Parsing (`DyldEngine::parse_rebase_stream`)
Previously, `DyldEngine` contained `apply_rebases()` for applying pre-parsed rebase records, but lacked an architectural `parse_rebase_stream()` method for decoding raw `LC_DYLD_INFO_ONLY` rebase bytes.
- **Implemented:** Full bytecode interpreter supporting all standard Darwin rebase opcodes.
- **Bounds Checking:** Every opcode modifying or using `seg_index` verifies `seg_index < segment_vmaddrs.size()`.
- **Count Limiting:** `DO_REBASE_ULEB_TIMES` and `DO_REBASE_ULEB_TIMES_SKIPPING_ULEB` enforce `count <= 65536` to prevent denial-of-service from corrupt or adversarial binaries.

### 2.2 ULEB128 and SLEB128 Safe Decoding
- **Risk:** Standard implementations of ULEB128/SLEB128 in C/C++ often loop until `(byte & 0x80) == 0`. An adversarial binary with infinite continuation bytes causes `shift >= 64`, resulting in undefined behavior in C++ (`<< 64` on 64-bit integers) or buffer overruns.
- **Protection:**
  ```cpp
  inline bool read_uleb128_safe(const std::uint8_t*& p, const std::uint8_t* end, std::uint64_t& out_val) noexcept {
      out_val = 0;
      int shift = 0;
      while (p < end) {
          std::uint8_t byte = *p++;
          if (shift > 63 || (shift == 63 && (byte & 0x7E) != 0)) return false;
          out_val |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
          if ((byte & 0x80) == 0) return true;
          shift += 7;
      }
      return false; // Truncated input
  }
  ```

### 2.3 Unterminated Symbol Name Protection
- **Risk:** In `BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM`, the symbol string is null-terminated. If the stream ends before a null byte is reached, an unhardened reader can run off the end of the Linkedit segment.
- **Protection:** Enforced explicit boundary check requiring a null byte before `end`. If `p == end` and no null byte was found, parsing fails with `"Unterminated symbol name in binding stream"`.

### 2.4 Weak Import Null Resolution
- In Apple Darwin dynamic linking, an unresolved symbol with `BIND_SYMBOL_FLAGS_WEAK_IMPORT` must resolve to `0ULL` (NULL) rather than causing an undefined symbol abort.
- `DyldEngine::apply_bindings` verifies:
  ```cpp
  if (!resolved.has_value()) {
      if (b.is_weak) {
          std::size_t off = translate_patch_vaddr(b.patch_vaddr, base_vaddr, memory.size());
          if (off + 8 <= memory.size()) {
              memory.write_u64(off, 0);
          }
          continue;
      }
      out_error = "Undefined dynamic symbol: " + b.symbol_name;
      return DyldLinkStatus::UndefinedSymbol;
  }
  ```

---

## 3. Test Verification Matrix

A dedicated test suite [`tests/unit/stage_192_4_dyld_binding_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_192_4_dyld_binding_tests.cpp) was developed and registered as target `stage_192_4_dyld_binding_tests`:

| Test # | Focus Area | Description | Status |
|---|---|---|---|
| **Test 1** | Rebase Opcode Stream Parsing | Parses and applies all 8 rebase opcodes to guest memory | **PASSED** |
| **Test 2** | Binding Opcode Stream Parsing | Parses and applies binding opcodes with addends to guest memory | **PASSED** |
| **Test 3** | OOB Segment Index Rejection | Rejection of out-of-bounds segment indices in rebase and bind | **PASSED** |
| **Test 4** | Unknown Opcode Rejection | Rejection of unrecognized opcodes (0xD0, 0xE0) | **PASSED** |
| **Test 5** | Unterminated Symbol Names | Detection of truncated symbol names in binding streams | **PASSED** |
| **Test 6** | ULEB128 Overflow & Truncation | Rejection of truncated and >64-bit overflowing ULEB streams | **PASSED** |
| **Test 7** | Weak Import Resolution | Verification that missing weak symbols resolve to NULL (`0x0`) | **PASSED** |
| **Test 8** | Negative SLEB128 Addend | Correct arithmetic resolution of negative pointer offsets | **PASSED** |

### Regression Target Execution:
- `stage_192_4_dyld_binding_tests`: **8 passed, 0 failed**
- `stage_192_3_chained_fixups_conformance_tests`: **7 passed, 0 failed**
- `stage_192_2_macho_hardening_tests`: **8 passed, 0 failed**
- `stage_u1_guest_minidyld_tests`: **9 passed, 0 failed**
- `stage_v0_chained_fixups_tests`: **3 passed, 0 failed**

---

## 4. Evidence Classification

- **Test Infrastructure:** Unit and integration tests (`UNIT`).
- **Binary Fixtures:** Independently compiled ARM64 Mach-O artifacts (`OPEN_SOURCE`, `PUBLIC_REFERENCE`).
- **Differential Verification:** Host `DyldEngine` vs Guest `minidyld` (`DIFFERENTIAL`).
- **Genuine Apple Kernel / iOS Boot:** **NONE** (Emulation strictly confined to userspace Mach-O EL0 loader structures).
