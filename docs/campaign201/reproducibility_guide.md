# WinIOS Campaign 201 — Stage 201.23 Reproducibility Guide

**Campaign:** 201  
**Stage:** 201.23  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target:** Step-by-step authoritative reproduction workflow for the WinIOS virtual platform  
**Date:** November 16, 2026  

---

## 1. System Prerequisites

To build, verify, and execute all tests and guest workloads cleanly:
- **Operating System:** Windows 11 x64 (Build 26100 or later)
- **Primary Toolchain:** Microsoft Visual Studio 2026 (MSVC v144+, C++20, `/W4 /WX /permissive-`)
- **Build System:** CMake 3.25+ and Ninja / MSBuild
- **Secondary Toolchain:** Zig 0.17.0 (or LLVM Clang supporting `-target aarch64-macos -mcpu=apple_a16`)
- **Shell Environment:** PowerShell 7+ or Windows PowerShell 5.1

---

## 2. Step-by-Step Reproduction Instructions

### Step 1: Toolchain and Repository Verification
Open PowerShell and verify the active repository and branch:
```powershell
cd D:\WinISO26\WinIOS
git status
git branch
```
*Expected branch:* `campaign-191r`.

### Step 2: Configure Build Directory
Configure MSVC C++20 build with strict warning flags:
```powershell
cmake -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_CXX_STANDARD=20
```

### Step 3: Compile Full Solution in Release Configuration
```powershell
cmake --build build --config Release --parallel
```
*Expected result:* 0 warnings, 0 errors.

### Step 4: Compile Full Solution in Debug Configuration
```powershell
cmake --build build --config Debug --parallel
```
*Expected result:* 0 warnings, 0 errors.

### Step 5: (Optional) Recompile Genuine ARM64e Guest Binary
If modifying guest workload sources:
```powershell
zig cc -target aarch64-macos -mcpu=apple_a16 -nostdlib -static -o tests/artifacts/bin/guest_stage200_end_to_end_workload tests/artifacts/src/guest_stage200_end_to_end_workload.S
```

### Step 6: Execute Full Regression Suite (Release & Debug)
Run the complete CTest regression test suite:
```powershell
# Release configuration
ctest --test-dir build -C Release --output-on-failure

# Debug configuration
ctest --test-dir build -C Debug --output-on-failure
```
*Expected outcome:* 100% of non-skipped tests passed (469 passed, 1 skipped by design: `#383 inferno_integration_tests`).

### Step 7: Run Campaign 201 Independent Verification Tests
Execute the Stage 201 verification executables directly:
```powershell
.\build\Release\stage201_5_arm64_instruction_differential_tests.exe
.\build\Release\stage201_6_sysreg_exception_differential_tests.exe
.\build\Release\stage201_7_mmu_stage1_differential_tests.exe
.\build\Release\stage201_8_stage2_combined_differential_tests.exe
.\build\Release\stage201_10_pac_independent_vector_tests.exe
.\build\Release\stage201_11_pac_instruction_conformance_tests.exe
.\build\Release\stage201_12_pac_context_isolation_tests.exe
.\build\Release\stage201_13_iommu_stress_tests.exe
.\build\Release\stage201_14_dma_safety_tests.exe
.\build\Release\stage201_15_gicv3_behavior_tests.exe
.\build\Release\stage201_16_smp_stress_tests.exe
.\build\Release\stage201_18_snapshot_restore_tests.exe
.\build\Release\stage201_19_snapshot_corruption_tests.exe
.\build\Release\stage201_20_deterministic_replay_tests.exe
.\build\Release\stage201_22_independent_guest_workload_tests.exe
```

### Step 8: Cryptographic Manifest Verification
Re-verify all file SHA-256 hashes against the manifest ledger:
```powershell
powershell -ExecutionPolicy Bypass -File scratch/verify_campaign201_manifest.ps1
```
*Expected outcome:* All artifacts matched with 0 discrepancies.
