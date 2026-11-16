# WinIOS Campaign 208: Git & Working Tree Audit Report

**Campaign:** 208 — XNU Source Fidelity, Genuine VM Bootstrap & Kernel Initialization  
**Branch:** `campaign-191r`  
**Host Toolchain:** Visual Studio 2026 MSVC C++20 / Zig 0.17.0  
**Baseline Commit:** `4a5572d67dcf2351bbebb517062c796632429e1b` (Campaign 207 Closeout)  
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
- `CMakeLists.txt`: Added new compilation targets for Campaign 208 runtime, allocator, provenance engine, and 16 unit test suites.
- `.gitignore`: Whitelisted `docs/campaign208/` and `artifacts/campaign-208/`.
- `src/kernel/xnu_arm_init.c`: Extended genuine XNU bootstrap logic to construct multi-level 16KB page tables (`arm_vm_init_early`) and invoke `pmap_bootstrap` for physical memory registration.
- `src/loader/xnu_genuine_init_builder.h` / `.cpp`: Added `load_or_get_image_208()` fallback pipeline loading the newly compiled Mach-O binary.
- `src/loader/embedded_xnu_image_208.inl`: Hex byte array embedding the 67,848-byte compiled Mach-O image.
- `src/loader/xnu_instruction_provenance_tracker.h` / `.cpp`: Instruction-level provenance tracker classifying retired guest instructions into Categories A, B, C, and D.
- `src/memory/xnu_page_allocator.h` / `.cpp`: 16KB physical frame allocator managing DRAM bounds `0x40000 - 0x800000`.

### Dedicated Campaign 208 Test Targets:
1. `tests/unit/stage208_5_instruction_provenance_tests.cpp`
2. `tests/unit/stage208_8_page_table_conformance_tests.cpp`
3. `tests/unit/stage208_9_real_page_table_walk_tests.cpp`
4. `tests/unit/stage208_10_physical_page_allocator_tests.cpp`
5. `tests/unit/stage208_11_bootstrap_vm_execution_tests.cpp`
6. `tests/unit/stage208_12_kernel_mapping_wx_tests.cpp`
7. `tests/unit/stage208_13_translation_reg_tlb_tests.cpp`
8. `tests/unit/stage208_14_exception_mmu_fault_tests.cpp`
9. `tests/unit/stage208_15_aic2_conformance_tests.cpp`
10. `tests/unit/stage208_16_timer_interrupt_delivery_tests.cpp`
11. `tests/unit/stage208_17_secondary_core_truth_tests.cpp`
12. `tests/unit/stage208_18_genuine_xnu_expansion_tests.cpp`
13. `tests/unit/stage208_19_kernel_init_state_machine_tests.cpp`
14. `tests/unit/stage208_20_snapshot_replay_expansion_tests.cpp`
15. `tests/unit/stage208_21_adversarial_security_tests.cpp`
16. `tests/unit/stage208_22_performance_instrumentation_tests.cpp`

---

## 3. Atomic Commit Policy

All modifications and additions for Campaign 208 are maintained locally on branch `campaign-191r`. Under no circumstances will any commits be pushed to a remote repository.
