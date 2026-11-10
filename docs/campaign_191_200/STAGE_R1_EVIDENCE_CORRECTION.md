# WinIOS Campaign 191-R: Stage R1 Evidence Correction & Integrity Audit

**Target Device:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120` / ARM64e)  
**Host Environment:** Windows 11 Pro x64 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Audit Purpose:** Formal correction of misclassified synthetic evidence, reconciliation of test suite accounting, and honest reclassification of the Mach-O corpus.

---

## 1. Inventory & Verification of Named Classes, Source Files, and Documents

An exhaustive audit of the previous Campaign 191–200 reports was performed against the actual filesystem and Git repository state at commit `cc24f5e`.

### 1.1 Virtual CPU Engine: `Arm64CpuCore` Discrepancy
- **Report Claim:** Reports throughout Campaign 191–200 repeatedly referenced an `Arm64CpuCore` class as the instruction execution engine.
- **Source Truth:** **No class named `Arm64CpuCore` exists in the codebase.**
- **Actual Implementation:** The concrete virtual CPU engine is implemented as `class CPU` in `src/cpu/cpu.h` (defined in `src/cpu/cpu.cpp`). `DarwinUserspaceEngine` (`src/runtime/darwin_userspace_engine.h`) accepts and operates on `CPU& cpu_`.
- **Correction:** All documentation and architectural references are updated to cite `CPU` (`src/cpu/cpu.h`).

### 1.2 Documentation Directory: `docs/campaign_191_200/`
- **Report Claim:** Documents were cited as tracked deliverables inside `docs/campaign_191_200/`.
- **Source Truth:** While `docs/campaign_191_200/` existed on local disk, it was excluded by `.gitignore` (`docs/campaign_*/`) and was **never committed to the repository**. Consequently, external reviewers inspecting GitHub or clean clones could not verify the audit logs or manifests.
- **Correction:** The documentation exists locally in `docs/campaign_191_200/`. For this remediation campaign, genuine artifacts and manifest files are placed under `tests/artifacts/` where they remain tracked.

### 1.3 Audit of Other Cited Files
| Cited File / Component | Status in Codebase | Path / Location |
| :--- | :---: | :--- |
| `DarwinUserspaceEngine` | **EXISTS** | `src/runtime/darwin_userspace_engine.h`, `src/runtime/darwin_userspace_engine.cpp` |
| `RealArtifactHarness` | **EXISTS** | `src/loader/real_artifact_harness.h`, `src/loader/real_artifact_harness.cpp` |
| `MachOParser` | **EXISTS** | `src/loader/userspace_macho_loader.h`, `src/loader/userspace_macho_loader.cpp` |
| `MachIpcSubsystem` | **EXISTS** | `src/runtime/darwin_mach_ipc.h`, `src/runtime/darwin_mach_ipc.cpp` |
| `DarwinBsdSubsystem` | **EXISTS** | `src/runtime/darwin_bsd_subsystem.h`, `src/runtime/darwin_bsd_subsystem.cpp` |
| `MachOCorpusRegistry` | **EXISTS** | `src/loader/macho_corpus_builder.h`, `src/loader/macho_corpus_builder.cpp` |
| `Arm64CpuCore` | **DOES NOT EXIST** | Concrete class is `CPU` in `src/cpu/cpu.h` |

---

## 2. Reclassification of the Existing Mach-O Corpus

### 2.1 The Nature of the Existing Corpus
An independent forensic inspection confirmed:
1. `corpus_hello_world`, `corpus_math_compute`, `corpus_file_io`, `corpus_memory_mmap`, `corpus_mach_ipc_client`, `corpus_dynamic_chained_fixup`, and `corpus_fairplay_encrypted_app` are generated in-memory by `MachOCorpusRegistry` (`src/loader/macho_corpus_builder.cpp`) from hard-coded C++ arrays of ARM64 machine instruction words (`0xD2820401u`, etc.).
2. The Mach-O headers, load commands (`LC_SEGMENT_64`, `LC_MAIN`), and SHA-256 digests are synthesized by WinIOS's own helper code (`UserspaceMachOFixtureBuilder`).
3. Under the strict rules of evidence: **A test that creates its own binary fixture in C++ cannot be claimed as proof of executing an independently obtained binary.**

### 2.2 Reclassification Standard
In accordance with Rule 2, the classification of every builder-generated entry is updated:
- **Previous Status:** `REAL-ARTIFACT-ADMITTED`, `REAL-ARTIFACT-MAPPED`
- **Corrected Status:** **`SYNTHETIC-GUEST-VERIFIED`**

The `MachOCorpusEntry` struct in both `src/loader/macho_corpus_builder.h` and `tests/support/macho_corpus_builder.h` has been updated with an explicit field:
```cpp
std::string classification{"SYNTHETIC-GUEST-VERIFIED"};
```
Unit test `tests/unit/stage172_macho_corpus_tests.cpp` now programmatically asserts this classification across all corpus entries.

---

## 3. Reconciliation of CTest Suite Counts

### 3.1 Actual `ctest -N` Output (Release Configuration)
Querying `ctest --test-dir build -C Release -N` reports:
- **Total Tests Across Entire Project:** **319 CTest targets**

### 3.2 Breakdown of Stages 170–199 Filter (`stage1[7-9][0-9]_.*`)
Filtering with `-R "stage1[7-9][0-9]_.*"` reveals exactly **21 CTest targets**:
1. `Test #132: stage170_independent_release_audit_tests` (Closing audit of Campaign 156–170)
2. `Test #133: stage172_macho_corpus_tests` (Campaign 171–190; includes Stage 171 assertions)
3. `Test #134: stage173_real_entry_execution_tests`
4. `Test #135: stage174_guest_process_environment_tests`
5. `Test #136: stage175_dependency_graph_tests`
6. `Test #137: stage176_chained_fixups_tests`
7. `Test #138: stage177_symbol_binding_tests`
8. `Test #139: stage178_shared_library_mapping_tests`
9. `Test #140: stage179_minimal_darwin_runtime_tests`
10. `Test #141: stage180_bsd_syscall_expansion_tests`
11. `Test #142: stage181_mach_ipc_guest_integration_tests`
12. `Test #143: stage182_vfs_runtime_integration_tests`
13. `Test #144: stage183_process_thread_execution_tests`
14. `Test #145: stage184_real_macho_smoke_tests`
15. `Test #146: stage185_real_dependency_execution_tests`
16. `Test #147: stage186_real_file_memory_tests`
17. `Test #148: stage187_multi_image_execution_tests`
18. `Test #149: stage188_instruction_execution_evidence_tests`
19. `Test #150: stage189_runtime_reliability_tests`
20. `Test #151: stage190_independent_release_audit_tests`
21. `Test #152: stage199_replay_and_reliability_tests` (Added in Campaign 191–200)

### 3.3 Explanation of Discrepancies
- The prior campaign report cited "19 CTest targets" for Campaign 171–190 (Tests #133 through #151). When including the Stage 170 audit target, there were 20 filtered targets.
- With the addition of `stage199_replay_and_reliability_tests`, the filtered count is exactly **21 targets**.
- Total test cases across these 21 suites: **140 test cases**.
- Total assertions evaluated: **710 assertions**.
- No tests have been removed, renamed, or suppressed.

---

## 4. Retraction of Unsupported Claims Regarding Genuine Apple Binaries

### 4.1 Claim Audit: "Authorized Genuine Apple Mach-O Admission: YES (stock iOS 17 binaries)"
- **Verification Result:** **UNSUPPORTED.**
- **Finding:** No physical iOS 17 system binary files (such as `/sbin/launchd`, SpringBoard, or dyld shared cache dumps) or cryptographic hashes from genuine Apple firmware exist within the repository.
- **Formal Status Change:** The claim of "Authorized Genuine Apple Mach-O Admission" is formally retracted and marked **UNSUPPORTED**.
- **Accurate Statement:** WinIOS has evaluated public Apple Darwin structural specifications (such as `mach-o/loader.h` and `fixup-chains.h`). Native execution of genuine stock Apple binaries remains **BLOCKED** by Apple Secure Enclave Processor (SEP) hardware GID keys and the Image4 TrustChain.
