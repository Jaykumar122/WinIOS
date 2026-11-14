# Stage 197.0 — Baseline, Repository Integrity & Evidence Audit

## 1. Executive Summary
This document establishes the verified starting baseline for **WinIOS Campaign 197: ARM64 Memory Model Fidelity, Atomic Instruction Correctness, Weak-Memory Simulation & SMP Conformance**.
Campaign 197 builds directly upon Campaign 196 (commit `f76ec480c5092c9375c00a3d86f8b92893be8753`), maintaining the strict invariant that Windows x64 host guarantees (TSO, sequentially consistent fences, mutexes) must not be confused with or substituted for guest architectural ARMv8-A weak memory semantics.

---

## 2. Repository & Environment Identity
- **Repository**: `D:\WinISO26\WinIOS\`
- **Branch**: `campaign-191r`
- **Baseline Git Commit**: `f76ec480c5092c9375c00a3d86f8b92893be8753` (short: `f76ec48`)
- **Host OS**: Windows 11 x64 (Build 26100)
- **Primary Toolchain**: Visual Studio 18 2026 MSVC C++20 (`/W4 /WX /permissive-`)
- **Secondary Cross-Compiler**: Zig 0.17.0 (LLVM/Clang target `aarch64-macos`, `aarch64-ios`, `aarch64-freestanding`)
- **Build System**: CMake 4.4.4, MSBuild 18.10.1
- **Research Device Identity**: iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)

---

## 3. Repository Integrity & Protected Files Audit
The repository was audited prior to commencing Stage 197:
- **Protected Files**:
  - `D:\WinISO26\WinIOS_README.md`: Untouched, verified intact.
  - `D:\WinISO26\WinIOS\docs\progress.md`: Untouched, verified intact.
  - `D:\WinISO26\WinISO26_README.md`: Untouched.
- **External Submodule**:
  - `D:\WinISO26\Inferno-Research\Inferno`: Verified pinned at commit `cc4302a99167abec69b714cfd00c38caece7e7de`, clean working tree.
- **Git Tree Status**:
  - Clean working tree at baseline `f76ec48`. Only `.gitignore` updated to track `docs/campaign197/`.

---

## 4. Baseline Regression Test Results

| Build Configuration | Total Tests | Passed | Skipped | Failed | Pass Rate | Baseline Output Log |
|:---|:---:|:---:|:---:|:---:|:---:|:---|
| **Release** | 403 | 402 | 1 | 0 | **100%** | `docs/campaign197/baseline_test_results_release.txt` |
| **Debug** | 403 | 402 | 1 | 0 | **100%** | `docs/campaign197/baseline_test_results_debug.txt` |

### Skipped Tests
- `371 - inferno_integration_tests`: Safely skipped as expected when the external Inferno research repository is unconfigured, preserving repository isolation.

### Compiler Diagnostics
- Zero compiler warnings or errors under MSVC `/W4 /WX /permissive-`.

---

## 5. Campaign 197 Strategic Technical Focus
1. Distinguish host implementation safety (C++ `std::mutex`, atomic fences) from guest ARM64 architectural memory-ordering guarantees.
2. Provide independent guest-level simulation and verification of ARM64 weak-memory ordering (`DMB`, `DSB`, `ISB`, `LDAR`, `STLR`).
3. Complete the ARMv8.1-A Large System Extensions (LSE) instruction matrix across 8/16/32/64-bit widths with acquire/release variants.
4. Ensure atomic operations reject partial memory modification upon address-translation or permission faults.
5. Create an independent weak-memory litmus test suite and compare permitted vs forbidden outcomes with documented reference criteria.
6. Verify 8-core SMP scheduling, deterministic replay, cross-core TLB invalidation, and interrupt/preemption interaction.
