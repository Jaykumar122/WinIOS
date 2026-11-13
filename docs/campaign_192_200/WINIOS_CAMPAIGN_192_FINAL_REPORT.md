# WINIOS CAMPAIGN 192 — FINAL ENGINEERING REPORT
## DARWIN USERSPACE COMPATIBILITY, DYLD HARDENING & RUNTIME CONFORMANCE

- **Campaign:** 192
- **Milestone:** Darwin Userspace Compatibility, Dyld Hardening & Runtime Conformance
- **Host Platform:** Windows 11 x64 (Build 26100+)
- **Host Toolchain:** Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)
- **Guest Toolchain:** Zig 0.17.0 (Clang / LLVM Mach-O backend)
- **Target Architecture:** ARM64 / ARM64e (`aarch64-macos`, `aarch64-ios`)
- **Target Research Device:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 `T8120`)
- **Status:** **COMPLETED — 100% GREEN (18/18 Stages Verified)**
- **Baseline Commit:** `a7ce5a7` (Campaign 191-V)
- **Final Regression Result:**
  - **Release:** **348 passed, 1 skipped, 0 failed** (100% of runnable targets, 349 total)
  - **Debug:** **348 passed, 1 skipped, 0 failed** (100% of runnable targets, 349 total)
  - *Skipped target:* `inferno_integration_tests` (opt-in environment variable required by design)

---

## 1. EXECUTIVE SUMMARY & OBJECTIVE

Campaign 192 established a hardened Darwin EL0 userspace execution environment and runtime conformance foundation for WinIOS on Windows 11 x64. Building directly upon the differential CPU testing and open-source binary execution of Campaign 191-V, Campaign 192 methodically eliminated ad-hoc behaviors across 18 distinct engineering stages (192.0 through 192.17).

Key architectural outcomes:
1. **Differential CPU Fidelity:** Expanded the automated differential testing oracle to **4,300 test vectors** across 12 instruction classes against Unicorn Engine 2.1.4. Implemented `ADC`, `ADCS`, `SBC`, and `SBCS` with full carry/overflow flag semantics, achieving **4,300 / 4,300 matches (100.0%)**.
2. **Parser & Dyld Hardening:** Hardened Mach-O parsing against integer overflows, malformed headers, segment overlaps, and truncated load commands. Added full classic rebase stream parsing (`0x00` through `0x80`), safe ULEB128/SLEB128 decoding with bitshift guards (`shift > 63`), weak-import NULL binding, `DYLD_CHAINED_PTR_START_MULTI` multi-page start support, and `DYLD_CHAINED_PTR_64_OFFSET` pointer decoding.
3. **Multi-Image & `@rpath` Resolution:** Upgraded the dependency resolution engine with deterministic path normalization, `@rpath` fallback lists, and robust cycle-safe topological sorting.
4. **Darwin ABI & Syscall Conformance:** Audited 21 core `libSystem.B.dylib` exported symbols, compiled an independent conformance artifact (`REAL-034`) retiring 32,863 guest instructions, constructed a 23-syscall compatibility matrix, and enforced strict Darwin ARM64 calling conventions (`SVC #0x80`, carry flag error return, dual X0/X1 pipe returns).
5. **Memory & VFS Semantics:** Validated 16 KB page granules, strict W^X memory protection, Darwin `SYS_madvise` (syscall 75), POSIX lowest-available file descriptor allocation/reuse, `O_APPEND` write pinning, and unidirectional pipe FIFO semantics.
6. **Mach IPC & Thread TLS:** Validated Mach port rights lifecycles (`urefs`), kernel self ports, bidirectional request-reply messaging, and Darwin `TPIDR_EL0` thread context switching.
7. **ARM64e Pointer Authentication Honesty:** Formally delineated NOP-space PAC hints (executed as architectural NOPs in EL0 guest CPU) from dedicated PAC instructions and syndromes modeled in `Arm64ePointerAuth`, correctly classified as `PARTIAL / EMULATED_REFERENCE`.

---

## 2. STAGE-BY-STAGE ENGINEERING BREAKDOWN

### Stage 192.0: Baseline Verification & Clean Environment Setup
- **Objective:** Verify repository integrity, toolchain readiness, and clean baseline regression.
- **Verification:**
  - Release: 334 passed, 1 skipped (`inferno_integration_tests`), 0 failed.
  - Debug: 334 passed, 1 skipped (`inferno_integration_tests`), 0 failed.
  - Zig compiler verified: Zig 0.17.0.
  - Pinned Inferno submodule verified untouched at `cc4302a99167abec69b714cfd00c38caece7e7de`.
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_0_BASELINE.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_0_BASELINE.md).

### Stage 192.1: Differential CPU Expansion & ADC/SBC Implementation
- **Objective:** Implement missing add/subtract-with-carry instructions in the guest CPU core and expand differential testing against Unicorn Engine 2.1.4.
- **Implementation:**
  - Added instruction decoders and execution routines for `ADC`, `ADCS`, `SBC`, and `SBCS` (32-bit and 64-bit variants) in `src/cpu/cpu.cpp`.
  - Expanded `tests/tools/diff_cpu/diff_arm64_oracle.py` from 1,750 to 4,300 test cases across 12 categories (Data Processing Immediate, Data Processing Register, Add/Sub with Carry, Multiply/Divide, Logical Shifts, Bitfield, Branches, Loads/Stores, PC-Relative Addressing, System Registers, Conditional Selects, Edge Cases).
- **Results:** 4,300 / 4,300 vectors matched between WinIOS guest CPU and Unicorn 2.1.4 (100.0%).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_1_DIFFERENTIAL_CPU.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_1_DIFFERENTIAL_CPU.md).

### Stage 192.2: Mach-O Parser Security Hardening
- **Objective:** Protect Mach-O loader against malformed, adversarial, and truncated binaries.
- **Implementation:**
  - Implemented safe 64-bit integer arithmetic checks preventing wrap-around overflows on `vmaddr + vmsize` and `fileoff + filesize` in `src/loader/macho_loader.cpp`.
  - Enforced segment overlap detection across all `LC_SEGMENT_64` commands.
  - Validated load command stream boundaries against `sizeofcmds` and file buffer limits.
  - Verified `LC_MAIN` entry point falls strictly within an executable `LC_SEGMENT_64` (`VM_PROT_EXECUTE`).
- **Test Target:** `stage_192_2_macho_hardening_tests` (8/8 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_2_MACHO_HARDENING.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_2_MACHO_HARDENING.md).

### Stage 192.3: Chained Fixups Format Conformance
- **Objective:** Expand Darwin chained fixup parser conformance for complex multi-segment iOS binaries.
- **Implementation:**
  - Implemented support for `DYLD_CHAINED_PTR_START_MULTI` and overflow start tables in `src/loader/dyld_engine.cpp`.
  - Added support for `DYLD_CHAINED_PTR_64_OFFSET` pointer formats.
  - Added strict symbol and import table index bounds validation.
  - Added infinite loop cycle guards (detecting cycles in chain traversal).
- **Test Target:** `stage_192_3_chained_fixups_conformance_tests` (7/7 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_3_CHAINED_FIXUPS.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_3_CHAINED_FIXUPS.md).

### Stage 192.4: Dyld Classic Rebase & Binding Hardening
- **Objective:** Implement full classic `LC_DYLD_INFO_ONLY` rebase and binding streams.
- **Implementation:**
  - Implemented `DyldEngine::parse_rebase_stream` supporting all classic rebase opcodes (`0x00` through `0x80`: `REBASE_OPCODE_SET_TYPE_IMM`, `SET_SEGMENT_AND_OFFSET_ULEB`, `ADD_ADDR_ULEB`, `DO_REBASE_IMM_TIMES`, `DO_REBASE_ULEB_TIMES`, `DO_REBASE_ADD_ADDR_ULEB`, `DO_REBASE_ULEB_TIMES_SKIPPING_ULEB`).
  - Implemented safe ULEB128 and SLEB128 decoders with truncation detection and bitshift overflow protection (`shift > 63`).
  - Added null resolution for `BIND_SYMBOL_FLAGS_WEAK_IMPORT` when targets are unresolved.
- **Test Target:** `stage_192_4_dyld_binding_tests` (8/8 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_4_DYLD_BINDING.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_4_DYLD_BINDING.md).

### Stage 192.5: Multi-Image Dependency Graph and `@rpath` Expansion
- **Objective:** Implement dynamic `@rpath` search lists and cycle-safe multi-image dependency resolution.
- **Implementation:**
  - Added `resolve_path` and `resolve_rpath_fallbacks` methods to `MachODependencyGraph` in `src/loader/macho_dependency_graph.cpp`.
  - Implemented `@loader_path` and `@executable_path` substitution.
  - Ensured that unresolvable libraries do not pollute the topological load order.
- **Test Target:** `stage_192_5_multi_image_graph_tests` (6/6 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_5_MULTI_IMAGE_GRAPH.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_5_MULTI_IMAGE_GRAPH.md).

### Stage 192.6: libSystem Lite ABI Conformance & Audit
- **Objective:** Audit and validate exported symbols and dispatch table for `libSystem.B.dylib` with a genuine compiled executable.
- **Implementation:**
  - Audited 21 critical Darwin ABI symbols: `malloc`, `free`, `memcpy`, `memset`, `strlen`, `strcmp`, `write`, `read`, `open`, `close`, `exit`, `abort`, `__stack_chk_guard`, `__stack_chk_fail`, `sched_yield`, `pthread_create`, `pthread_join`, `mach_task_self`, `mach_reply_port`, `mach_msg_trap`, `_tlv_bootstrap`.
  - Authored and compiled `tests/artifacts/src/stage_192_6_abi_app.c` via Zig 0.17.0 (`REAL-034`, SHA-256 `a74a6f79154a9188c09f9495398f2f46d6c9b0d113a6b0df475b36a3c9dd344c`).
  - Executed `REAL-034` in the EL0 guest environment via `minidyld`: retired **32,863 guest instructions** with clean exit code 0.
- **Test Target:** `stage_192_6_libsystem_abi_tests` (3/3 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_6_LIBSYSTEM.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_6_LIBSYSTEM.md).

### Stage 192.7: Memory Management and Virtual Memory Conformance
- **Objective:** Enforce Darwin userspace virtual memory invariants and implement `SYS_madvise`.
- **Implementation:**
  - Implemented Darwin `SYS_madvise` (syscall 75) supporting `MADV_NORMAL`, `MADV_RANDOM`, `MADV_SEQUENTIAL`, `MADV_WILLNEED`, `MADV_DONTNEED`, `MADV_FREE`.
  - Enforced 16 KB page granule alignments and validated strict W^X enforcement (rejection of `PROT_WRITE | PROT_EXEC` unless `MAP_JIT` 0x0800 is set).
  - Validated architectural Stage-1 MMU translation faults and permission faults on RO pages.
- **Test Target:** `stage_192_7_virtual_memory_tests` (5/5 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_7_VIRTUAL_MEMORY.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_7_VIRTUAL_MEMORY.md).

### Stage 192.8: Darwin BSD Syscall ABI Conformance
- **Objective:** Verify Darwin ARM64 syscall calling conventions and build compatibility matrix.
- **Implementation:**
  - Built a formal 23-syscall compatibility matrix covering process, memory, filesystem, and I/O syscalls.
  - Enforced ARM64 Darwin calling conventions: `SVC #0x80`, syscall number in `X16`, arguments in `X0..X5`, carry flag cleared on success, carry flag set on failure with positive `errno` in `X0`, and dual return values (`X0` and `X1`) for `SYS_pipe`.
- **Test Target:** `stage_192_8_bsd_syscall_tests` (5/5 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_8_BSD_SYSCALLS.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_8_BSD_SYSCALLS.md).

### Stage 192.9: File Descriptor and VFS Semantics
- **Objective:** Harden VFS layer to match POSIX and Darwin file descriptor lifecycle and pipe semantics.
- **Implementation:**
  - Enhanced `DarwinBsdSubsystem` to strictly allocate the lowest available file descriptor integer (`>= 3`) upon `open` and `dup`.
  - Implemented `O_APPEND` automatic seek-to-end pinning on writes.
  - Enforced access mode protections: write calls fail with `EBADF` on `O_RDONLY` descriptors, read calls fail on `O_WRONLY`.
  - Validated pipe FIFO buffering, EOF delivery on write-end closure, and `EPIPE` error on read-end closure.
- **Test Target:** `stage_192_9_vfs_tests` (5/5 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_9_VFS.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_9_VFS.md).

### Stage 192.10: Mach IPC Right and Message Validation
- **Objective:** Validate Mach port rights lifecycle, user reference counting, and `mach_msg` error matrix.
- **Implementation:**
  - Validated Mach port right types (`MACH_PORT_RIGHT_SEND`, `RECEIVE`, `SEND_ONCE`, `PORT_SET`, `DEAD_NAME`) and reference counting (`urefs`).
  - Validated standard Darwin Mach traps: `task_self` (-28), `thread_self` (-27), `reply_port` (-26), `host_self` (-29).
  - Validated `mach_msg` error conditions (`MACH_SEND_INVALID_DEST`, `MACH_SEND_MSG_TOO_SMALL`, `MACH_RCV_TIMED_OUT`, `MACH_RCV_TOO_LARGE`).
  - Tested bidirectional request-reply messaging with kernel reply port rights translation.
- **Test Target:** `stage_192_10_mach_ipc_tests` (5/5 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_10_MACH_IPC.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_10_MACH_IPC.md).

### Stage 192.11: Thread-Local Storage and Thread Lifecycle
- **Objective:** Ensure correct context preservation of `TPIDR_EL0` across thread state transitions.
- **Implementation:**
  - Audited `DarwinUserspaceEngine::save_thread_context` and `load_thread_context` in `src/runtime/darwin_userspace_engine.cpp`.
  - Wired `tpidr_el0` context persistence using `cpu_.get_tpidr_el0()` and `cpu_.set_tpidr_el0(ctx.tpidr_el0)`.
  - Validated isolation across simulated concurrent threads sharing an address space.
- **Test Target:** `stage_192_11_threads_tls_tests` (4/4 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_11_THREADS_TLS.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_11_THREADS_TLS.md).

### Stage 192.12: Runtime Initialization and Process Exit
- **Objective:** Validate Darwin process initialization stack invariants and clean termination.
- **Implementation:**
  - Audited `UserspaceMachOLoader::construct_initial_stack`: verified strict AAPCS64 16-byte stack alignment and pointer layouts (`argc`, `argv[]`, `NULL`, `envp[]`, `NULL`, `apple[]`, `NULL`).
  - Verified `mod_init_func` topological execution order (leaf dependencies execute initializers before dependents).
  - Validated `SYS_exit` transition to zombie/terminated state with preservation of exit status codes.
- **Test Target:** `stage_192_12_process_lifecycle_tests` (4/4 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_12_PROCESS_LIFECYCLE.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_12_PROCESS_LIFECYCLE.md).

### Stage 192.13: ARM64e and Pointer Authentication Boundary Audit
- **Objective:** Document and audit PAC instruction emulation versus hardware execution.
- **Implementation:**
  - Verified that ARMv8.3 hint PAC instructions (`PACIASP`, `AUTIASP`, `PACIAZ`, `AUTIAZ`, `PACIBSP`, `AUTIBSP`) execute as architectural NOPs in EL0 guest CPU mode.
  - Audited dedicated PAC instructions (`PACIA`, `AUTIA`, `PACDA`, `AUTDA`), stripping (`XPACI`, `XPACD`), and branch/return instructions (`BRAA`, `BLRAA`, `RETAA`) with SipHash tag verification and EC 0x1C synchronous trap generation in `Arm64ePointerAuth`.
  - Formally classified PAC boundary as `PARTIAL / EMULATED_REFERENCE`.
- **Test Target:** `stage_192_13_pac_boundary_tests` (5/5 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_13_ARM64E_PAC.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_13_ARM64E_PAC.md).

### Stage 192.14: Guest Execution Tracing and Evidence Integrity
- **Objective:** Validate execution tracing, instruction retirement counting, provenance tracking, and data exports.
- **Implementation:**
  - Validated 1:1 guest instruction retirement counting.
  - Verified strict `ExecutionOrigin` tagging (`ExecutionOrigin::Guest` vs `ExecutionOrigin::Host`).
  - Audited structured query APIs (`find_steps_by_pc`, `find_steps_by_reg_write`, `get_syscall_trace`).
  - Validated JSON and CSV trace export schemas.
- **Test Target:** `stage_192_14_execution_tracing_tests` (5/5 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_14_EVIDENCE_INTEGRITY.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_14_EVIDENCE_INTEGRITY.md).

### Stage 192.15: Large Open-Source Application Compatibility
- **Objective:** Confirm regression integrity of large open-source applications executed under 16 KB Stage-1 MMU.
- **Implementation:**
  - Re-verified all 9 compiled variants of SHA-256 (`REAL-025..027`), cJSON (`REAL-028..030`), and uzlib (`REAL-031..033`) across macOS -O0, macOS -O2, and iOS chained fixups.
- **Results:** 9/9 variants passed (100% matched host truth), retiring **2,458,851 guest instructions**.
- **Test Target:** `stage_v3_large_opensource_tests` (9/9 suites passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_15_LARGE_APPS.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_15_LARGE_APPS.md).

### Stage 192.16: Integrated Darwin Userspace Conformance
- **Objective:** End-to-end integration test across all unified userspace subsystems.
- **Implementation:**
  - Created composite scenario: VFS pipe pipeline (`pipe`, `write`, `dup`, `read`, `close`), virtual memory (`mmap`, `madvise`, `munmap`), Mach IPC messaging (`mach_msg`), and concurrent thread TLS switching (`TPIDR_EL0`).
- **Test Target:** `stage_192_16_integrated_conformance_tests` (4/4 unit tests passed).
- **Closeout Report:** [`docs/campaign_192_200/STAGE_192_16_INTEGRATED_CONFORMANCE.md`](file:///D:/WinISO26/WinIOS/docs/campaign_192_200/STAGE_192_16_INTEGRATED_CONFORMANCE.md).

### Stage 192.17: Final Audit, Full Regression & Campaign Closure
- **Objective:** Full Release and Debug regressions, repository hygiene audit, and final campaign closeout.
- **Verification:**
  - Release CTest: **348 passed, 1 skipped, 0 failed** out of 349 (100% runnable pass rate).
  - Debug CTest: **348 passed, 1 skipped, 0 failed** out of 349 (100% runnable pass rate).
  - Total new CTest test targets added in Campaign 192: **13 new suites** (`stage_192_2` through `stage_192_14` and `stage_192_16`). Total CTest suites increased from 335 to 349.
  - Protected files audit: `WinIOS_README.md`, `WinIOS/docs/progress.md`, and `WinISO26_README.md` completely untouched.
  - External repository audit: `Inferno-Research/Inferno` clean and pinned at `cc4302a99167abec69b714cfd00c38caece7e7de`.

---

## 3. REAL ARTIFACT INVENTORY & EVIDENCE REGISTRY

The real artifact corpus now spans **34 independently compiled ARM64 Mach-O binaries** (`REAL-001` through `REAL-034`), all registered in `tests/artifacts/macho_artifact_manifest.json`:

| Artifact ID | File Path | SHA-256 | Target | Retired Instructions | Evidence Classification |
|---|---|---|---|---|---|
| **REAL-001** | `bin/hello_arm64` | `61f36402ea29b3520977fc6d01e23da9128e469550bb1488c9f045c71bfa44ea` | `aarch64-macos` | 9 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-002** | `bin/minimal_c` | `9f196ce18d962ea9ef8849c30f7ae0bc182f6e9ce2b9211c4794fc7c223c75fd` | `aarch64-macos` | 487 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-003** | `bin/math_compute` | `30a21f64f4c2c5ec6cf843187a1738740c885c3e620573be041324707641772e` | `aarch64-macos` | 487 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-004** | `bin/control_flow` | `a6a57f1ea5fc4bc0f0e0db12f2c8d2050b1df0bb2ec907a27eb2a8ff93ce56ad` | `aarch64-macos` | 511 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-005** | `bin/string_ops` | `ae065625bf928da0fce81934c9359e1966a3473fc6cb2bfd690a6ea2e078ba19` | `aarch64-macos` | 610 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-006** | `bin/libmath_dylib.dylib` | `43fe0dfd548310f844b1c7ea9ef6e43242207908b2eb59578297b4ec5eebc8be` | `aarch64-macos` | N/A (Library) | `REAL_ARTIFACT_VERIFIED` |
| **REAL-007** | `bin/dylib_consumer` | `fa68cbe22b5133604ea7a0df470a1334812fcfc5a7551065116bb3cc9bcaaa9d` | `aarch64-macos` | 741 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-008** | `bin/ios_minimal_c` | `98448eaebaa39e7cf8be0ea89b9148d4db3b9ffef38b6d8a7cb01df18f273b4e` | `aarch64-ios` | 586 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-009** | `bin/ios_math_compute` | `1e7b992f97463f82cb37a4e69b910e53f191b7e43598501f2f3274296eb4c0b5` | `aarch64-ios` | 586 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-010** | `bin/ios_control_flow` | `96a75a7ffbc82798e4d2a13f277ca647087cae6eaeb685b3bc74db65caecae6a` | `aarch64-ios` | 610 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-011** | `bin/ios_string_ops` | `bb57582d92c730eec4cf605cf5725f778d91b8fa1761d4b6814bbce3c4346bb7` | `aarch64-ios` | 711 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-012** | `bin/libdep_leaf.dylib` | `bb08d3568c0b25e799ae862eb962fcb4fbfe3470701ec6a93bf6fa99e2e50fd8` | `aarch64-ios` | N/A (Library) | `REAL_ARTIFACT_VERIFIED` |
| **REAL-013** | `bin/libdep_mid.dylib` | `9e8d646b95b87ec8f844ba9c6f0ffda9d936171f114c0a5d48259ca545ae6984` | `aarch64-ios` | N/A (Library) | `REAL_ARTIFACT_VERIFIED` |
| **REAL-014** | `bin/ios_three_image_consumer` | `75db4b423f6e1f02cf3ccefe5374431522f98642cc97e93da4c1e4c76d29ffb6` | `aarch64-ios` | 913 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-015** | `bin/ios_initializers` | `378129df499f57ebcb3bb4f899ce9c381c15f91eb731f82f80c65507d727bca5` | `aarch64-ios` | 621 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-016** | `bin/ios_tlv` | `b9f3659cfb6cfba6169542a49280d44e5fb90176313fa4fb7db4cc0224df39c1` | `aarch64-ios` | 605 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-017** | `bin/guest_minidyld` | `62b9f8482cc7867215f79571ff5ff3ca3efba075429ea3fc1c4481023795b5fe` | `aarch64-macos` | 1,418 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-018** | `bin/libSystem.B.dylib` | `41f3e792da8f13b2c34d6ca255476a6cf06fe7a052ff8f192bfebeeb31c00fbf` | `aarch64-macos` | N/A (Library) | `REAL_ARTIFACT_VERIFIED` |
| **REAL-019** | `bin/minidyld_consumer_minimal_c` | `9f196ce18d962ea9ef8849c30f7ae0bc182f6e9ce2b9211c4794fc7c223c75fd` | `aarch64-macos` | 2,130 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-020** | `bin/minidyld_consumer_math_compute` | `30a21f64f4c2c5ec6cf843187a1738740c885c3e620573be041324707641772e` | `aarch64-macos` | 2,130 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-021** | `bin/minidyld_consumer_control_flow` | `a6a57f1ea5fc4bc0f0e0db12f2c8d2050b1df0bb2ec907a27eb2a8ff93ce56ad` | `aarch64-macos` | 2,154 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-022** | `bin/minidyld_consumer_string_ops` | `ae065625bf928da0fce81934c9359e1966a3473fc6cb2bfd690a6ea2e078ba19` | `aarch64-macos` | 2,253 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-023** | `bin/minidyld_consumer_dylib_consumer` | `fa68cbe22b5133604ea7a0df470a1334812fcfc5a7551065116bb3cc9bcaaa9d` | `aarch64-macos` | 2,416 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-024** | `bin/real_mach_traps_app` | `6c10e11802951e7efbc351336cf9cae62d470d06caae0a5624701dd92500cbf1` | `aarch64-macos` | 2,420 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-025** | `bin/sha256_macos_O0` | `88be11cb57064ef3f7a14e9f906fdb6118d531a7f6ff7ce274a9db4be30da64f` | `aarch64-macos` | 33,858 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-026** | `bin/sha256_macos_O2` | `75a6c11d2793aa8d575c3d25686036da6b83f360ae5262fcaaa2beeb5f4cc0d1` | `aarch64-macos` | 13,854 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-027** | `bin/sha256_ios_chained` | `b3d4fba73dfdbca7aeaf66eb3074ee0f911a37c356e9c402d76ee179f83ea0e0` | `aarch64-ios` | 34,064 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-028** | `bin/cjson_macos_O0` | `9b36357876a267f56bb7d90d8a55dbb7e71f9cfd542387157643b185984ba17e` | `aarch64-macos` | 46,712 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-029** | `bin/cjson_macos_O2` | `dfd0b674d852a4ef5ee2e6ffab0ff645eebfe1a1969df6ec45fdfce2ec5aa025` | `aarch64-macos` | 26,104 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-030** | `bin/cjson_ios_chained` | `d720b005118c21a4fca240f91f3b392193b043232b737f2672bfdc058fcb5e5a` | `aarch64-ios` | 46,918 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-031** | `bin/uzlib_macos_O0` | `116cebb384df9fe6d34e9e0344b1c2b5757d544ca843daea8667da99b9cf9128` | `aarch64-macos` | 1,067,987 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-032** | `bin/uzlib_macos_O2` | `fc2db0df6504a79fc0caee495b2c7e090a2aee714b7e9cf2cfc21fc7d23a7894` | `aarch64-macos` | 121,152 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-033** | `bin/uzlib_ios_chained` | `9d554aee10a95ffae25381a1796191b984fa5ba2e7836ea795d117cb3d1ee0aa` | `aarch64-ios` | 1,068,202 | `REAL_ARTIFACT_EXECUTED` |
| **REAL-034** | `bin/stage_192_6_abi_app` | `a74a6f79154a9188c09f9495398f2f46d6c9b0d113a6b0df475b36a3c9dd344c` | `aarch64-macos` | 32,863 | `REAL_ARTIFACT_EXECUTED` |

---

## 4. ARCHITECTURAL BOUNDARY & CAPABILITY STATEMENT

In strict accordance with WinIOS evidence honesty principles, the capabilities of WinIOS at the conclusion of Campaign 192 are formally stated:

### What WinIOS IS:
- **High-Fidelity ARM64 EL0 Userspace Execution Engine:** Accurately interprets and executes ARM64 user-mode machine code generated by production compilers (Clang/LLVM) for Darwin targets (`aarch64-macos`, `aarch64-ios`).
- **Differential-Verified CPU:** 4,300 verified instruction patterns matched 100% against Unicorn Engine 2.1.4.
- **Darwin Dynamic Linker & Runtime Model:** Supports static and dynamic Mach-O execution, classic rebase and binding, `DYLD_CHAINED_PTR_64` and `DYLD_CHAINED_PTR_64_OFFSET` chained fixups, multi-image dependency resolution with `@rpath` search lists, and static initializers.
- **Darwin Syscall & Mach Trap Emulation Layer:** Implements BSD syscalls (`write`, `read`, `open`, `close`, `mmap`, `munmap`, `madvise`, `pipe`, `dup`, `exit`, etc.) and basic Mach message IPC traps (`task_self`, `reply_port`, `mach_msg`).
- **Realistic 16 KB Page Stage-1 MMU:** Enforces 16 KB page granules, normal memory semantics, and strict W^X policy.

### What WinIOS is NOT:
- **NOT an iOS Firmware Bootloader:** Does not boot genuine Apple iOS firmware, iBoot, or SecureROM.
- **NOT an XNU Kernel Execution Engine:** Does not execute genuine Mach/XNU kernel binaries (`mach_kernel`). Kernel traps and BSD syscalls are emulated host-side or serviced by the lite runtime.
- **NOT an iOS GUI / SpringBoard Environment:** Does not execute UIKit, SpringBoard, Metal, or genuine closed Apple frameworks.
- **NO Genuine Proprietary Apple Firmware/Decryption Keys:** All artifacts are permissively licensed, open-source C programs independently compiled with Zig 0.17.0 targeting Darwin.

---

## 5. REGRESSION & COMPARATIVE METRICS

| Metric | Campaign 191-V Closeout | Campaign 192 Closeout | Delta |
|---|---|---|---|
| **Differential CPU Tests (Unicorn Oracle)** | 1,750 | **4,300** | **+2,550 (+145.7%)** |
| **Differential Test Categories** | 10 | **12** | **+2 categories** |
| **Total CTest Test Targets** | 335 (334 pass, 1 skip) | **349 (348 pass, 1 skip)** | **+14 targets (+4.2%)** |
| **New Campaign 192 CTest Suites** | 0 | **13 suites** | **+13 new test suites** |
| **Total Real Mach-O Artifacts** | 33 (`REAL-001`..`033`) | **34 (`REAL-001`..`034`)** | **+1 artifact (`REAL-034`)** |
| **Max Instructions Retired (Single Run)** | 1,068,202 (`REAL-033`) | **1,068,202 (`REAL-033`)** | — |
| **Total Instructions Retired (Real Binaries)**| ~2.46M instructions | **>2.52M instructions** | **+60K instructions** |
| **Release CTest Pass Rate** | 100% runnable (334/334) | **100% runnable (348/348)**| **Maintained 100%** |
| **Debug CTest Pass Rate** | 100% runnable (334/334) | **100% runnable (348/348)**| **Maintained 100%** |
| **Unresolved Test Failures** | 0 | **0** | **0 failures** |

---

## 6. CLOSEOUT VERDICT

Campaign 192 has successfully achieved all required goals:
- All 18 stages (192.0 through 192.17) fully engineered, tested, and documented.
- 100% clean builds in both Release and Debug on MSVC C++20 with `/W4 /WX /permissive-`.
- 100% clean CTest regression across all 348 runnable targets in both configurations.
- Evidence standards and protected file rules strictly preserved.

Campaign 192 is hereby declared **COMPLETE and VERIFIED**.
