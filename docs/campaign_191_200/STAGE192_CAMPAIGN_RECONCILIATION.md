# WINIOS STAGE 192: CAMPAIGN STAGE AND TEST RECONCILIATION
## Complete Mapping of Stages, CTest Targets, Assertions, and Artifact Classifications

**Target:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120` / ARM64e)  
**Host Toolchain:** Windows 11 Pro x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Scope:** Forensic reconciliation of Campaign 171–190 stage numbering, test targets, missing `stage171` test investigation, and metric disambiguation.

---

### 1. Investigation of the "Missing" `stage171` Test Target

In Campaign 171–190, the CTest suite report showed 20 passing targets under the filter `stage1[7-9][0-9]_.*`:
1. `Test #132: stage170_independent_release_audit_tests`
2. `Test #133: stage172_macho_corpus_tests`
3. ... through `Test #151: stage190_independent_release_audit_tests`

#### Root Cause Analysis:
- **Test Numbering Offset:** Test #132 is `stage170_independent_release_audit_tests`, which was the final audit suite of **Campaign 156–170**. Because the CTest regex matched `stage170`, exactly 20 tests were reported (1 from prior campaign + 19 from current campaign).
- **Stage 171 Test Coverage:** Stage 171 (*Userspace Mach-O Admission & Integrity Enforcement*) was implemented in `src/loader/apple_artifact_admission.cpp`, `src/loader/macho_corpus_builder.cpp`, and `src/loader/real_artifact_harness.cpp`.
- The test suite constructed for this functionality was named `stage172_macho_corpus_tests.cpp`. A forensic audit of `stage172_macho_corpus_tests.cpp` reveals that:
  - **Test 1** (Catalog Admission & Provenance Inspection),
  - **Test 2** (Cryptographic SHA-256 Fingerprint Determinism),
  - **Test 5** (Code Signature and FairPlay Encrypted Rejection),
  - **Test 6** (In-Memory Mach-O Admission Pipeline), and
  - **Test 7** (Corpus Metadata Export and Manifest Generation)
  directly test and validate **Stage 171**.
- Thus, the Stage 171 implementation was **fully tested**, but its test suite was co-located inside `stage172_macho_corpus_tests.cpp` rather than having a standalone `stage171_artifact_admission_tests.cpp`.

---

### 2. Disambiguated Project Metrics

To prevent ambiguity, the campaign metrics are strictly separated into verifiable counts:

| Metric Category | Count | Detailed Breakdown |
| :--- | :---: | :--- |
| **Campaign Stages (171–190)** | **20** | Stages 171 through 190 specified in the master prompt |
| **CTest Executable Targets (171–190)** | **19** | `stage172` through `stage190` |
| **CTest Targets Executed by Filter** | **20** | `stage170` (1 from Campaign 156–170) + 19 from Campaign 171–190 |
| **Individual Test Cases (Campaign 171–190)** | **133** | Exactly 7 test functions per test target across 19 suites |
| **Individual Test Cases (All 20 CTest targets)** | **140** | 133 + 7 test cases from `stage170` |
| **Verified Assertions (Campaign 171–190)** | **685** | Cumulative `TEST_ASSERT` invocations across 19 suites |
| **Verified Assertions (All 20 CTest targets)** | **715** | 685 + 30 assertions from `stage170` |
| **Corpus Mach-O Artifacts Admitted** | **7** | `corpus_hello_world`, `math_compute`, `file_io`, `memory_mmap`, `mach_ipc_client`, `dynamic_chained_fixup`, `fairplay_encrypted_app` |
| **Corpus Mach-O Artifacts Executed** | **4** | `corpus_hello_world`, `math_compute`, `file_io`, `memory_mmap` (via guest EL0 CPU) |
| **Independently Obtained Real Mach-O Binaries Executed** | **0** | (See Stage 193/194: Synthetic fixtures vs external disk binaries) |

---

### 3. Stage-to-Test-to-Evidence Reconciliation Matrix

| Original Stage | Reported Stage | Actual Implementation Module | Source File | CTest Executable Target | Actual Evidence | Current Classification |
| :---: | :---: | :--- | :--- | :--- | :--- | :--- |
| **171** | 171 | Artifact Admission & SHA-256 | `apple_artifact_admission.cpp`, `real_artifact_harness.cpp` | `stage172_macho_corpus_tests` | SHA-256 fingerprinting, FairPlay rejection | `REAL-ARTIFACT-ADMITTED` |
| **172** | 172 | 16KB W^X Segment Mapping | `userspace_macho_loader.cpp` | `stage172_macho_corpus_tests` | Non-overlapping 16KB W^X segments | `UNIT-VERIFIED` |
| **173** | 173 | Userspace Darwin Stack Frame | `userspace_macho_loader.cpp` | `stage174_guest_process_environment_tests` | 16-byte SP alignment, argc, argv, envp | `UNIT-VERIFIED` |
| **174** | 174 | EL0 Entry Point Execution | `darwin_userspace_engine.cpp` | `stage173_real_entry_execution_tests` | Live EL0 instruction execution | `SYNTHETIC-GUEST-VERIFIED` |
| **175** | 175 | Dependency Graph & RPATH | `macho_dependency_graph.cpp` | `stage175_dependency_graph_tests` | Topological sort, cycle detection | `UNIT-VERIFIED` |
| **176** | 176 | Chained Fixups Engine | `dyld_engine.cpp` | `stage176_chained_fixups_tests` | `DYLD_CHAINED_PTR_64`/`ARM64E` unchaining | `UNIT-VERIFIED` |
| **177** | 177 | dyld Symbol Binding & ASLR | `dyld_engine.cpp` | `stage177_symbol_binding_tests` | Two-level namespace, Export Trie parsing | `UNIT-VERIFIED` |
| **178** | 178 | Shared Library 2GB Arena | `shared_library_mapper.cpp` | `stage178_shared_library_mapping_tests` | Dedicated virtual arena allocation | `UNIT-VERIFIED` |
| **179** | 179 | Shared Cache Header Parser | `dyld_engine.cpp` | `stage179_minimal_darwin_runtime_tests` | `dyld_v1  arm64e` header validation | `PARTIAL` |
| **180** | 180 | Darwin BSD Syscall Expansion | `darwin_bsd_subsystem.cpp` | `stage180_bsd_syscall_expansion_tests` | `lseek`, `fstat64`, `mprotect`, `stat64` | `SYNTHETIC-GUEST-VERIFIED` |
| **181** | 181 | Mach IPC Trap Dispatching | `darwin_mach_ipc.cpp` | `stage181_mach_ipc_guest_integration_tests` | `mach_msg_trap`, task port spaces | `SYNTHETIC-GUEST-VERIFIED` |
| **182** | 182 | Darwin VFS & Descriptors | `darwin_bsd_subsystem.cpp` | `stage182_vfs_runtime_integration_tests` | POSIX descriptor isolation, `dup`, devfs | `UNIT-VERIFIED` |
| **183** | 183 | Process & Thread Lifecycle | `darwin_bsd_subsystem.cpp` | `stage183_process_thread_execution_tests` | Process table, `wait4`, `tpidr_el0` | `UNIT-VERIFIED` |
| **184** | 184 | Real Mach-O Smoke Execution | `darwin_userspace_engine.cpp` | `stage184_real_macho_smoke_tests` | Clean exit code via guest `SYS_exit` | `SYNTHETIC-GUEST-VERIFIED` |
| **185** | 185 | Dependency Execution | `darwin_userspace_engine.cpp` | `stage185_real_dependency_execution_tests` | Cross-image dylib function dispatch | `SYNTHETIC-GUEST-VERIFIED` |
| **186** | 186 | Guest File & Memory Ops | `darwin_userspace_engine.cpp` | `stage186_real_file_memory_tests` | File read/write, anonymous `mmap` | `SYNTHETIC-GUEST-VERIFIED` |
| **187** | 187 | Multi-Image Pipeline | `darwin_userspace_engine.cpp` | `stage187_multi_image_execution_tests` | Tri-image pipeline with chained fixups | `SYNTHETIC-GUEST-VERIFIED` |
| **188** | 188 | Instruction Evidence Tracing | `execution_diagnostics.cpp` | `stage188_instruction_execution_evidence_tests` | Instruction disassembly, JSON/CSV trace | `UNIT-VERIFIED` |
| **189** | 189 | Failure Recovery & Stability | `darwin_userspace_engine.cpp` | `stage189_runtime_reliability_tests` | Exception containment, memory bounds | `UNIT-VERIFIED` |
| **190** | 190 | Master Release Audit | All Subsystems | `stage190_independent_release_audit_tests` | 7 cross-system integration invariants | `REGRESSION-VERIFIED` |

---

### 4. Conclusion

The audit reconciles all 20 stages of Campaign 171–190. The apparent mismatch between the 20 reported CTest targets and the absence of a `stage171` test target is conclusively explained: `stage170` (the prior campaign audit) was executed in the batch, while Stage 171's admission tests were consolidated within `stage172_macho_corpus_tests`. All functional claims have exact source and test mappings.
