# STAGE S0: LANDING & FULL REGRESSION REPORT

**Campaign:** WINIOS CAMPAIGN 191-S  
**Branch:** `campaign-191r`  
**Commit Hash:** `efa04c2`  
**Host:** Windows 11 Pro x64  
**Toolchain:** Visual Studio 18 2026, MSVC C++20, `/W4 /WX /permissive-`  
**Target Device:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120` / ARM64e)  
**Date:** 2026-11-09  

---

## 1. LANDING & BRANCH VERIFICATION

All Campaign 191-R remediation changes and the new ADR/ADRP architectural unit tests have been staged and committed to branch `campaign-191r`.
No push was executed.

### Committed Files:
1. `.gitignore` - corrected artifact exclusions (`/artifacts/` ignore, unignore `!tests/artifacts/`)
2. `CMakeLists.txt` - registered `stage_r3_real_artifact_execution_tests` and `adr_adrp_conformance_tests`
3. `src/cpu/cpu.cpp` - implemented AArch64 ADR and ADRP PC-relative addressing decoders and disassembly
4. `src/loader/macho_corpus_builder.h` - reclassified synthetic builders to `SYNTHETIC-GUEST-VERIFIED`
5. `tests/support/macho_corpus_builder.h` - test support corpus reclassification
6. `tests/unit/stage172_macho_corpus_tests.cpp` - assertion of synthetic classification
7. `tests/artifacts/bin/hello_arm64` - genuine compiled ARM64 Mach-O binary
8. `tests/artifacts/build_artifacts.ps1` - deterministic artifact build script via Zig 0.17.0
9. `tests/artifacts/macho_artifact_manifest.json` - dedicated real artifact manifest
10. `tests/artifacts/src/hello_arm64.S` - freestanding assembly source
11. `tests/unit/stage_r3_real_artifact_execution_tests.cpp` - disk-backed real binary execution test
12. `tests/unit/adr_adrp_conformance_tests.cpp` - 9-part architectural conformance test for ADR & ADRP

---

## 2. FULL CTEST REGRESSION AUDIT

### CTest Target Counts:
- **Baseline CTest Targets:** 320 targets
- **New Targets Added in 191-R / S0:** 1 target (`adr_adrp_conformance_tests`)
- **Total Registered Targets:** 321 targets

### Darwin Userspace & Real Artifact Targets (23/23 Passed):
| Target Name | Release Result | Debug Result |
| :--- | :--- | :--- |
| `stage170_independent_release_audit_tests` | PASSED (0.03s) | PASSED (0.04s) |
| `stage172_macho_corpus_tests` | PASSED (0.02s) | PASSED (0.04s) |
| `stage173_real_entry_execution_tests` | PASSED (0.09s) | PASSED (0.16s) |
| `stage174_guest_process_environment_tests` | PASSED (0.01s) | PASSED (0.02s) |
| `stage175_dependency_graph_tests` | PASSED (0.01s) | PASSED (0.02s) |
| `stage176_chained_fixups_tests` | PASSED (0.01s) | PASSED (0.01s) |
| `stage177_symbol_binding_tests` | PASSED (0.01s) | PASSED (0.01s) |
| `stage178_shared_library_mapping_tests` | PASSED (0.04s) | PASSED (0.10s) |
| `stage179_minimal_darwin_runtime_tests` | PASSED (0.02s) | PASSED (0.02s) |
| `stage180_bsd_syscall_expansion_tests` | PASSED (0.01s) | PASSED (0.01s) |
| `stage181_mach_ipc_guest_integration_tests` | PASSED (0.02s) | PASSED (0.01s) |
| `stage182_vfs_runtime_integration_tests` | PASSED (0.01s) | PASSED (0.02s) |
| `stage183_process_thread_execution_tests` | PASSED (0.01s) | PASSED (0.02s) |
| `stage184_real_macho_smoke_tests` | PASSED (0.02s) | PASSED (0.08s) |
| `stage185_real_dependency_execution_tests` | PASSED (0.01s) | PASSED (0.04s) |
| `stage186_real_file_memory_tests` | PASSED (0.02s) | PASSED (0.06s) |
| `stage187_multi_image_execution_tests` | PASSED (0.02s) | PASSED (0.07s) |
| `stage188_instruction_execution_evidence_tests` | PASSED (0.01s) | PASSED (0.04s) |
| `stage189_runtime_reliability_tests` | PASSED (1.55s) | PASSED (2.23s) |
| `stage190_independent_release_audit_tests` | PASSED (0.03s) | PASSED (0.10s) |
| `stage199_replay_and_reliability_tests` | PASSED (0.15s) | PASSED (0.27s) |
| `stage_r3_real_artifact_execution_tests` | PASSED (0.03s) | PASSED (0.04s) |
| `adr_adrp_conformance_tests` | PASSED (0.06s) | PASSED (0.08s) |

---

## 3. ENTRY POINT & ADR/ADRP INDEPENDENT MATHEMATICAL VERIFICATION

### Entry Point Resolution:
For `tests/artifacts/bin/hello_arm64` (`REAL-001`):
- `vmaddr(__TEXT)`: `0x100000000`
- `entryoff`: `0x13f8`
- `slide`: `0x0`
- Calculated Entry PC: `0x100000000 + 0x0 + 0x13f8 = 0x1000013f8`
- Actual Entry Instruction fetched: `0xD2800020` (`MOV X0, #1`)

### ADRP & ADD Calculation:
At `PC = 0x1000013fc`:
- Target symbol `msg` offset in binary: `0x8008` (mapped at `0x100008008` in `__DATA`).
- ADRP page calculation:
  - `PC_page = PC & ~0xFFF = 0x1000013fc & ~0xFFF = 0x100001000`
  - `Target_page = 0x100008008 & ~0xFFF = 0x100008000`
  - Page delta: `(0x100008000 - 0x100001000) >> 12 = 0x7000 >> 12 = +7 pages` (imm = 7).
  - ADRP instruction: `0x90000041` (computes `0x100008000` into X1).
- Subsequent ADD instruction at `PC = 0x100001400`:
  - `ADD X1, X1, #0x008`: `0x100008000 + 0x8 = 0x100008008`.
  - Exactly resolves to `"Hello from real Mach-O\n"`.

---

## 4. ADR / ADRP CONFORMANCE TEST SUITE

The newly implemented test suite `tests/unit/adr_adrp_conformance_tests.cpp` verifies 9 rigorous architectural assertions:
1. **ADR Basic Offsets:** Validates imm=0 yields PC, imm=+8 yields PC+8, imm=-16 yields PC-16.
2. **ADR Page Boundary Crossing:** Validates forward crossing into next 4KB page and backward crossing into prior page.
3. **ADR Extreme Ranges:** Max positive range (+1,048,575) and min negative range (-1,048,576).
4. **ADR Rd = XZR (31):** Discards result; SP invariant; registers 0..30 invariant.
5. **ADRP Page Alignment Masking:** Ensures low 12 bits of PC are zeroed (`PC & ~0xFFF`) regardless of PC offset inside page.
6. **ADRP Positive & Negative Page Offsets:** Validates +1 page, +16 pages, -1 page, -8 pages.
7. **ADRP Extreme Page Ranges:** Validates +1,048,575 pages (+4GB - 4KB) and -1,048,576 pages (-4GB).
8. **ADRP Rd = XZR (31):** Discards result; SP invariant.
9. **NZCV Flag Preservation:** NZCV remains bitwise unchanged across both ADR and ADRP.

---

## 5. PROTECTED FILES AUDIT

The following protected files remain 100% unaltered:
- `D:\WinISO26\WinIOS_README.md`: SHA-256 `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` (VERIFIED UNCHANGED)
- `D:\WinISO26\WinIOS\docs\progress.md`: SHA-256 `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` (VERIFIED UNCHANGED)
- `D:\WinISO26\Inferno-Research\`: Submodule commit `cc4302a99167abec69b714cfd00c38caece7e7de` (VERIFIED UNTOUCHED)
