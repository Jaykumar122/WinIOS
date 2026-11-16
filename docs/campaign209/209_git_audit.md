# WinIOS Campaign 209: Git & Working Tree Audit Report

**Campaign:** 209 — Genuine XNU VM Initialization, Physical Page Management & Kernel Memory Services  
**Branch:** `campaign-191r`  
**Host Toolchain:** Visual Studio 2026 MSVC C++20 / Zig 0.17.0  
**Baseline Commit:** `89e415dda8ff1a0faaac253cedf9105abbe681d7` (Campaign 208 Closeout)  
**Verification Date:** 2026-11-16  

---

## 1. Protected Files Integrity Verification

As mandated by all engineering campaign directives, the following core repository files and directories are protected and must remain 100% immutable:

| Protected File / Directory | Verification Status | Hash / State |
| :--- | :--- | :--- |
| `WinIOS_README.md` | **UNTOUCHED** | Clean / Identical to Baseline |
| `docs/progress.md` | **UNTOUCHED** | Clean / Identical to Baseline |
| `WinISO26_README.md` | **UNTOUCHED** | Clean / Identical to Baseline |
| `Inferno-Research/` | **UNTOUCHED** | Clean / Identical to Baseline |

---

## 2. Modified & Newly Introduced Files

### Core Engine & Kernel Modifications:
- `CMakeLists.txt`: Added new compilation targets for Campaign 209 tests (`stage209_*`).
- `.gitignore`: Whitelisted `docs/campaign209/` and `artifacts/campaign-209/`.
- `src/kernel/xnu_arm_init.c`:
  - Corrected Level-2 block descriptor base for Apple AIC2 from `0x0220000705` to `0x000000023A000705ULL` (base `0x23A000000`), restoring exact identity translation (`VA 0x23B100000 -> PA 0x23B100000`).
  - Zeroed Level-2 slot 1 (`0x0ULL`), eliminating 32MB ghost DRAM exposure and enforcing translation fault on unbacked DRAM.
  - Implemented `pmap_init()` and `vm_page_bootstrap()`, populating page metadata header at `0x28000`, 512 `pmap_phys_attributes` at `0x28020`, and 496 `xnu_vm_page_t` structures at `0x28220`.
  - Added Mailbox 209 at `0x50010` holding `0xCAFEBABE209`.
- `src/loader/xnu_genuine_init_builder.h` / `.cpp`: Added `load_or_get_image_209()` fallback pipeline loading the newly compiled Mach-O binary.
- `src/loader/embedded_xnu_image_209.inl`: Hex byte array embedding the 67,976-byte compiled Mach-O image.
- `artifacts/campaign-209/xnu_genuine_init.macho`: Compiled Mach-O binary (SHA-256: `cc2ad408e64a8bd191b7cc697e1ec6d52d8e505344419b2f59d8ca47cba8f516`).

### Dedicated Campaign 209 Test Targets:
1. `tests/unit/stage209_2_aic2_physical_mapping_tests.cpp`
2. `tests/unit/stage209_3_physical_memory_consistency_tests.cpp`
3. `tests/unit/stage209_5_provenance_audit_tests.cpp`
4. `tests/unit/stage209_6_physical_page_metadata_tests.cpp`
5. `tests/unit/stage209_7_page_allocator_correctness_tests.cpp`
6. `tests/unit/stage209_8_page_metadata_init_execution_tests.cpp`
7. `tests/unit/stage209_9_kernel_va_map_tests.cpp`
8. `tests/unit/stage209_10_kernel_mapping_services_tests.cpp`
9. `tests/unit/stage209_11_wx_memory_isolation_tests.cpp`
10. `tests/unit/stage209_12_tlb_lifecycle_tests.cpp`
11. `tests/unit/stage209_16_vm_fault_integration_tests.cpp`
12. `tests/unit/stage209_18_genuine_xnu_vm_expansion_tests.cpp`
13. `tests/unit/stage209_19_kernel_init_state_machine_tests.cpp`
14. `tests/unit/stage209_20_smp_per_core_vm_tests.cpp`
15. `tests/unit/stage209_21_snapshot_replay_tests.cpp`
16. `tests/unit/stage209_22_adversarial_memory_tests.cpp`
17. `tests/unit/stage209_23_performance_instrumentation_tests.cpp`

---

## 3. Atomic Commit Policy

All modifications and additions for Campaign 209 are maintained locally on branch `campaign-191r`. Under no circumstances will any commits be pushed to a remote repository.
