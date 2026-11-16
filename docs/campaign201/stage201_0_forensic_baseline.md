# WinIOS Campaign 201 — Stage 201.0 Forensic Baseline Audit

**Campaign:** 201  
**Stage:** 201.0  
**Repository:** `D:\WinISO26\WinIOS\`  
**Date:** November 16, 2026  
**Auditor:** Principal ARM64/ARM64e Virtual Platform Systems Engineer  

---

## 1. Executive Summary

This forensic baseline audit establishes the exact repository state, commit lineage, artifact provenance, and critical discrepancies inherited from Campaign 200.

The repository was verified on Windows 11 x64 under the official toolchain:
- **Host OS:** Windows 11 x64 (Build 10.0.26100)
- **Primary Toolchain:** Microsoft Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`)
- **Secondary Toolchain:** Zig 0.17.0 (`-target aarch64-macos -mcpu=apple_a16`)
- **Active Branch:** `campaign-191r`
- **Current HEAD:** Commit `c569f42dfa6812322d7ddc7050b0adbc4e8aadfb`
- **Parent Commit:** `950c3d5dfd5eb37e70dd06a1c27c0d05383c85a2` (Campaign 199 closeout)
- **Working Tree:** Clean (`nothing to commit, working tree clean`)

---

## 2. Git Commit & Lineage Verification (Issue B Resolution)

A complete inspection of Git log, reflog, and object database confirms:
1. **Commit Hash:** `c569f42dfa6812322d7ddc7050b0adbc4e8aadfb` (short: `c569f42`).
2. **Commit Subject:** `WinIOS Campaign 200: Advanced ARM64e integration, architectural conformance, security validation, and genuine guest execution`.
3. **Commit Timestamp:** `Mon Nov 16 01:02:46 2026 +0530`.
4. **Parent Commit:** `950c3d5dfd5eb37e70dd06a1c27c0d05383c85a2` (matches Campaign 200 baseline expectation).
5. **Committed Contents:** 42 files changed, 6,930 insertions(+), 13 deletions(-):
   - `CMakeLists.txt` (114 additions registering `stage200_3` through `stage200_23`)
   - `src/cpu/cpu.cpp` (12 lines modified for dual-mode PAC exception handling)
   - `src/cpu/pointer_auth.cpp` (16 lines modified for PAC fault reporting)
   - 21 unit test suites (`tests/unit/stage200_*.cpp`)
   - 2 genuine workload files (`tests/artifacts/src/guest_stage200_end_to_end_workload.S` and binary)
   - 14 documentation files under `docs/campaign200/`
   - 1 utility script (`scratch/generate_manifest.ps1`)
   - 2 full test logs (`test_results_release.txt` and `test_results_debug.txt`)
6. **Post-Commit Modifications:** None. The working directory is completely clean and matches the committed state.

---

## 3. Critical Discrepancies Inherited from Campaign 200

### Issue A: CTest Count Ambiguity
- **Reported:** 470 total test targets, 470 passed, 0 failures, 1 skipped.
- **Forensic Fact:** 470 total test targets are registered in `CMakeLists.txt`. Test #383 (`inferno_integration_tests`) is skipped at runtime by CMake design because external research assets are absent.
- **Root Cause:** CTest outputs `100% tests passed out of 470` because CTest calculates the pass percentage as `passed / (total - skipped) = 469 / 469 = 100%`. The report author mistakenly wrote "470 passed, 1 skipped", which sum to 471.
- **Mathematically True Result:** 470 registered, 469 executed and passed, 0 failed, 1 skipped.

### Issue C: CPU Core Topology Discrepancy
- **Reported:** iPhone 15 A16 Bionic 8-core CPU topology (2 performance Everest + 6 efficiency Sawtooth cores).
- **Physical Hardware Ground Truth:** Apple A16 Bionic (`T8120`, `D37AP`) physically possesses **6 CPU cores**:
  - 2 Performance cores (Everest, up to 3.46 GHz)
  - 4 Efficiency cores (Sawtooth, up to 2.02 GHz)
- **Virtual Execution Profile:** WinIOS supports up to **8 virtual CPU cores** via `SmpScheduler` / `SMPCluster` (`Core 0` through `Core 7`).
- **Correction Required:** WinIOS documentation and tests must cleanly decouple the **physical target profile (6 cores: 2P+4E)** from the **configurable virtual execution profile (up to 8 virtual cores)**.

### Issue D: Snapshot / Replay Scope
- **Reported:** Complete serialization of CPU, MMU, TLB, IOMMU, GICv3, RAM, and multicore state.
- **Forensic Fact:** The `.wsnap` v2 format serializes metadata, core directory, per-core architectural state (registers, PC, SP, system registers), shared RAM chunks, scheduler state, exclusive monitor reservations, interrupt router state, and device state blocks.
- **Verification Requirement:** Prove independently that non-CPU components (IOMMU mappings, GIC priority masks, TLB entries) are restored identically across capture-restore-replay cycles.

### Issue E: Pointer Authentication (PAC) Cryptographic Algorithm
- **Reported:** "ARM64e PAC (QARMA/QARMA-64 cryptographic signing)".
- **Forensic Fact:** Inspection of `src/cpu/pointer_auth.cpp` lines 43–68 reveals that `compute_pac_tag` executes **SipHash-3** rounds using the standard SipHash initialization constants (`0x736f6d6570736575` = "somepseu", `0x646f72616e646f6d` = "dorandom", etc.), producing a 16-bit MAC in bits [63:48].
- **Truth Statement:** WinIOS implements a **SipHash-based truncated PRF**, NOT the ARM architectural QARMA-64 / QARMA-5 block cipher. This must be clearly documented in all architecture reports.

### Issue F: Architectural Conformance Categorization
- All assertions of Apple or ARM conformance must strictly separate:
  1. *Implemented*: Code exists in WinIOS codebase.
  2. *Unit-tested*: Verified by unit tests.
  3. *Integration-tested*: Verified across multiple subsystems.
  4. *Differentially tested*: Compared against an independent reference.
  5. *Specification-derived*: Derived from ARM DDI 0487 reference manual.
  6. *Approximated*: Behavior modeled with deliberate simplifications (e.g. SipHash PAC tag, synthetic DTB).
  7. *Unsupported*: Hardware features omitted or stubbed.

---

## 4. Baseline Audit Verdict

The baseline commit `c569f42dfa6812322d7ddc7050b0adbc4e8aadfb` is authentic, completely reproducible, and forms a stable foundation for the independent verification and platform truth objectives of Campaign 201.
