# WINIOS CAMPAIGN 271: MASTER ENGINEERING EXECUTION FINAL REPORT

## Genuine Upstream `load_context`, Kernel Bootstrap Continuation, Thread-State Construction & Guest Execution Certification
**Campaign:** 271  
**Stages:** Exactly 25 sequential engineering stages (271.0 through 271.24)  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Campaign 270 Baseline HEAD:** `54fb9bdab98402893e6df63c9d715db4aa8ca7ec`  
**Expected Parent (C269):** `21662d8a0e02957812405e34cc5c8d9e743f2850`  
**Expected Grandparent (C268):** `e5bb6b92afc524f90cd1fd49ad1d736a2979a75c`  
**Host Architecture:** Windows 11 x64, MSVC C++20 (`/W4 /WX /permissive-`)  
**Target Profile:** ARM64 / ARM64e Research Compatibility Profile (iPhone 15 `iPhone15,4`, `D37AP`, Apple A16 `T8120`, 16 KiB Translation Granule)  
**Pinned Public Upstream Reference:** Apple Public XNU `xnu-10002.1.13`

---

## 1. Executive Verdict & Mandatory Reporting Standard

> **"Campaign 271 reconstructed the upstream `load_context` contract and its dependencies, but did not establish genuine upstream context-transition execution. The platform remains at the verified early-initialization/WFI milestone."**

---

## 2. Executive Summary & Core Architectural Findings

Campaign 271 was launched to investigate and resolve the fundamental context-transition gap identified in Campaign 270:
1. **Historical Binary Control-Flow Reality:**
   - In `xnu_genuine_init.macho` (Campaign 210 Mach-O), guest execution begins at `_start` (`0x2BE0`), establishes the bootstrap stack (`SP = 0x10000`), calls `_arm_init` (`0x250C`), which calls `_machine_startup` (`0x21D4`).
   - `_machine_startup` initializes early kernel subsystems (kmem, zones, early scheduler headers, initial task/thread records) and then **returns linearly** back to `_arm_init`, which returns to `_start` (`0x2C10`).
   - Upon return, `_start` executes `wfi` at `0x2C14` and enters an infinite branch loop `b 1b` (`0x2C18`).
   - Neither `_load_context` nor `_kernel_bootstrap` is linked in `xnu_genuine_init.macho`.
2. **Debunking Address `0x10000`:**
   - In Campaign 270's harness emulation, control was transferred to address `0x10000` under the synthetic label of `kernel_bootstrap`.
   - Campaign 271's binary audit proved that in `xnu_genuine_init.macho`, `0x10000` is the address of `bootstrap_stack_top` and `_g_arm_init_state` located in segment `__DATA`, section `__common`!
   - This address has memory protection `initprot = 3` (`VM_PROT_READ | VM_PROT_WRITE`) and strictly `VM_PROT_EXECUTE = 0`.
   - Branching to `0x10000` constitutes an invalid branch into data, which would immediately trigger an instruction execution permission fault under strict W^X enforcement.
3. **Upstream Source Audit (`xnu-10002.1.13`):**
   - The upstream source `osfmk/kern/thread.h` defines:
     `extern void Load_context(thread_t thread) __attribute__((noreturn));`
   - In upstream XNU, `kernel_bootstrap()` creates `kernel_bootstrap_thread` and calls `load_context(thread)`, jumping permanently into the continuation without returning.
   - The low-level ARM64 implementation resides in `osfmk/arm64/cswitch.s` (with entry points in `locore.s`).
   - The minimum architectural context-restore contract requires:
     - `X0` = incoming `thread_t` pointer.
     - `TPIDR_EL1` updated with incoming `thread_t` via `msr tpidr_el1, x0` (`0xD518D080`).
     - `SP_EL1` pivoted to `thread->machine.kstackptr` via `mov sp, x2` (`0x9100005F`).
     - `X1` loaded with `thread->continuation` (`0xF9402001`).
     - `X0` loaded with `thread->parameter` (`0xF9402400`).
     - Terminal non-returning branch via `br x1` (`0xD61F0020`).
4. **Enhanced Architectural Adaptation & Guest Execution:**
   - WinIOS constructed and executed the byte-exact 6-instruction adaptation in the guest instruction engine.
   - Stepping through all 6 native instructions verified that `TPIDR_EL1` received `0x60000`, `SP_EL1` transitioned from `0xfffffe0007038000` to `0xfffffe0007044000` (Core 0 Thread Stack, Frames 22..23), `X0` received `0x0`, and `PC` branched to `0x8000`.
   - However, because the continuation code at `0x8000` in the test harness is synthetic (Category D) and the genuine public XNU `kernel_bootstrap` is unlinked in the binary, WinIOS **explicitly rejects** claiming genuine upstream continuation execution.
5. **Maturity Verdict:**
   - **Level 3 (Autonomous Upstream XNU Early Initialization): Formally Retained and Certified.**
   - **Level 4 (User-Space Launch / Mach Subsystem Bootstrap): Explicitly Rejected.**

---

## 3. Detailed Stage-by-Stage Engineering Execution (Stages 271.0 – 271.24)

### Stage 271.0: Baseline Discovery and Repository Protection Gate
- Full Git HEAD: `54fb9bdab98402893e6df63c9d715db4aa8ca7ec` (Campaign 270 Closeout).
- Parent (HEAD~1): `21662d8a0e02957812405e34cc5c8d9e743f2850` (Campaign 269 Closeout).
- Grandparent (HEAD~2): `e5bb6b92afc524f90cd1fd49ad1d736a2979a75c` (Campaign 268 Closeout).
- Branch: `campaign-191r`.
- Cryptographic Invariants Verified:
  - `WinIOS_README.md`: SHA-256 `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` (Bit-exact match).
  - `docs/progress.md`: SHA-256 `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` (Bit-exact match).
  - `WinISO26_README.md`: Non-existent (Verified).
  - `Inferno-Research/`: Zero tool access, 100% strictly isolated.
- Toolchain: MSVC 14.51.36231 (Visual Studio 18 2026), CMake 4.4.4, C++20 `/W4 /WX /permissive-`.

### Stage 271.1: Campaign 270 Implementation Inventory
- Mach-O Image File: `embedded_xnu_image_210.inl` (`xnu_genuine_init.macho`), 68,728 bytes.
- Total Symbols in Image: 109 symbols.
- Key Functions Linked:
  - `_start` / `__start`: `0x2BE0`
  - `_arm_init`: `0x250C`
  - `_machine_startup`: `0x21D4`
  - `_thread_init`: `0x1F84`
  - `_task_init`: `0x1D88`
  - `_sched_init`: `0x1D30`
  - `_zone_bootstrap`: `0x1B6C`
  - `_kmem_init`: `0x19A0`
  - `_vm_page_bootstrap`: `0x18FC`
  - `_pmap_init`: `0x16F4`
  - `_pmap_bootstrap`: `0x1540`
  - `_arm_vm_init_early`: `0x14F0`
- Symbols Missing (Unlinked): `_load_context`, `_Load_context`, `_kernel_bootstrap`.
- Debunking 0x10000: Symbol `_g_arm_init_state` is at `0x10000` in `__DATA,__common` (RW, non-executable).

### Stage 271.2: Pinned Upstream `load_context` Source Audit
- Pinned Public XNU: `xnu-10002.1.13`.
- Prototype: `extern void Load_context(thread_t thread) __attribute__((noreturn));` in `osfmk/kern/thread.h`.
- High-level caller: `osfmk/kern/startup.c::kernel_bootstrap()`.
- Assembly Implementation: `osfmk/arm64/cswitch.s` and companion entry points in `locore.s`.
- Non-returning semantics: Never returns to `machine_startup`; branches directly into continuation routine.

### Stage 271.3: Upstream Context-Switch ABI Reconstruction
- Register Contract:
  - Input: `X0` receives `thread_t`.
  - Continuation: `X1` loaded with `thread->continuation`.
  - Stack Pointer: `SP_EL1` restored from `thread->machine.kstackptr`.
  - Argument: `X0` loaded with `thread->parameter`.
  - Thread Pointer: `TPIDR_EL1` updated with `X0`.
- Stack Alignment: Strictly 16-byte aligned (AAPCS64).
- PSTATE / DAIF: Executes at `EL1h` with `DAIF.I = 1` during atomic transition.

### Stage 271.4: Campaign 270 Emulation Comparison
- Discrepancy Matrix:
  1. SP Transition: C270 pivoted SP from `0xfffffe0007038000` to `0xfffffe0007044000` (Required Host Adaptation).
  2. X0 Parameter: C270 set X0 = parameter (Exact Match).
  3. Destination PC: C270 targeted `0x10000` (Incorrect: `0x10000` is non-executable data in Mach-O).
  4. LR: C270 did not preserve or clear LR (Incomplete).
  5. Thread Pointer: C270 omitted `TPIDR_EL1` update (Incomplete).
  6. PAC Authentication: C270 did not authenticate continuation (Unimplemented Dependency).
  7. Provenance: C270 was synthetic harness code (Category D), not compiled binary execution.

### Stage 271.5: `load_context` Dependency Closure
- 12 dependency nodes categorized into 6 dependency classes:
  - Missing Link Dependencies: `_Load_context`, `_kernel_bootstrap_thread` (2 nodes).
  - Missing Destination Dependency: Executable page at continuation PC (1 node).
  - Satisfied Minimum Context Transition Dependencies: EL1 mode, `DAIF.I=1`, valid `thread_t`, stack backing, 16-byte alignment.
  - Scheduler Prerequisites: Multi-thread run queues, priority decay, and preemption timers decoupled from initial single-thread cold restore.

### Stage 271.6: Genuine `kernel_bootstrap` Source and Symbol Audit
- Source: `osfmk/kern/startup.c::kernel_bootstrap()`.
- Binary Check: Confirmed absent from `xnu_genuine_init.macho`.
- Address `0x10000` Segment Check: Located in `__DATA` (vmaddr `0x8000`, vmsize `0x80c8`), section `__common`. `initprot = 0x3` (RW). Execute bit `0x4` is strictly unset.

### Stage 271.7: Historical Mach-O Control-Flow Audit
- Disassembled `0x2BE0` through `0x2C20`:
  - `0x2BE0: msr daifset, #0xf`
  - `0x2BE4: mrs x20, mpidr_el1`
  - `0x2BE8: and x20, x20, #0xff`
  - `0x2BEC: cbnz x20, 0x2c1c`
  - `0x2BF0: mov x2, #0x300000`
  - `0x2BF4: msr cpacr_el1, x2`
  - `0x2BF8: isb`
  - `0x2BFC: adrp x1, 0x10000`
  - `0x2C00: add x1, x1, #0`
  - `0x2C04: mov sp, x1`
  - `0x2C08: mov x19, x0`
  - `0x2C0C: mov x0, x19`
  - `0x2C10: bl 0x250c` (`_arm_init`)
  - `0x2C14: wfi`
  - `0x2C18: b 0x2c18` (`1: b 1b`)
- Proves that `_arm_init` returns linearly to `0x2C14` and enters an infinite idle loop.

### Stage 271.8: Build-System and Assembly Integration Audit
- Toolchain: MSVC `cl.exe` does not assemble Apple/GNU ARM64 assembly files directly.
- Direct compilation of `osfmk/arm64/cswitch.s` in isolation fails due to missing `assym.s` (generated by `genassym.c`) and Mach headers.
- Evaluated adaptation: Model byte-exact native ARM64 opcodes directly in loader/runtime.

### Stage 271.9: Upstream Thread-State Initialization Audit
- Evaluated `kernel_bootstrap_thread` state contract:
  - Stack: `0xfffffe0007040000`..`0xfffffe0007044000` (16 KiB).
  - SP Alignment: 16 bytes.
  - Parameter: `0x0` (NULL).
  - PSTATE: `0x3C5` (EL1h, `DAIF=0xF`).

### Stage 271.10: Processor and Scheduler Prerequisites
- Audited minimum prerequisites before cold thread load:
  - Master processor data structure initialized.
  - `active_thread` field updated.
  - Processor status marked `PROCESSOR_RUNNING`.
  - `TPIDR_EL1` register loaded.

### Stage 271.11: Memory and Stack Ownership for the Real Transition
- Initial Boot Stack: `0xfffffe0007038000` (Core 0 Interrupt Stack, Frame 20).
- Destination Thread Stack: `0xfffffe0007044000` (Core 0 Thread Stack, Frames 22..23).
- Guard Page Protection: Frame 19 (`0x4C000`) and Frame 21 (`0x54000`) verified as guard pages.
- AAPCS64 Alignment: Both pointers strictly 16-byte aligned.

### Stage 271.12: ARM64 and ARM64e Context Metadata Audit
- Canonical Frame Size: 288 bytes (`arm_saved_state64_t`).
- ARM64e Extensions: Preserves opaque PAC metadata (APIBKeyLo/Hi, signed continuations); avoids manufactured hardware claims.

### Stage 271.13: Implement or Adapt Upstream `load_context`
- Constructed byte-exact 6-instruction adaptation adding `TPIDR_EL1` update:
  - `0xD518D080`: `msr tpidr_el1, x0`
  - `0xF9402001`: `ldr x1, [x0, #0x40]`
  - `0xF9400002`: `ldr x2, [x0, #0x00]`
  - `0xF9402400`: `ldr x0, [x0, #0x48]`
  - `0x9100005F`: `mov sp, x2`
  - `0xD61F0020`: `br x1`

### Stage 271.14: Independent Instruction Encoding and Disassembly Verification
- Byte-level decoding confirmed:
  - `0xD518D080`: System instruction `MSR TPIDR_EL1, X0` (Op0=3, Op1=0, CRn=13, CRm=0, Op2=4, Rt=0).
  - `0xF9402001`: `LDR X1, [X0, #0x40]` (imm12=8).
  - `0xF9400002`: `LDR X2, [X0, #0x00]` (imm12=0).
  - `0xF9402400`: `LDR X0, [X0, #0x48]` (imm12=9).
  - `0x9100005F`: `MOV SP, X2` (ADD imm, Rd=31/SP, Rn=2, imm=0).
  - `0xD61F0020`: `BR X1` (Unconditional branch reg, Rn=1).

### Stage 271.15: Linker and Final-Binary Integration Audit
- Verified absence of `_load_context` and `_kernel_bootstrap` in `xnu_genuine_init.macho`.
- Verified strict W^X policy: `__TEXT` segment has `initprot = 5` (RX), with zero writable executable pages.

### Stage 271.16: Guest Execution of `load_context`
- Stepped all 6 instructions in guest CPU engine:
  - Initial State: `PC=0x2000`, `SP=0xfffffe0007038000`, `X0=0x60000`.
  - Instruction Retirement: 6 native ARM64 instructions retired.
  - Final State: `PC=0x8000`, `SP=0xfffffe0007044000`, `X0=0x0`, `TPIDR_EL1=0x60000`.

### Stage 271.17: Genuine `kernel_bootstrap` Destination Execution Audit
- Provenance Classification: Destination `0x8000` is Category D (Synthetic Test Harness).
- Public upstream `kernel_bootstrap` is unlinked in the binary.
- Hard Acceptance Rule: WinIOS explicitly refuses to certify upstream `kernel_bootstrap` execution.

### Stage 271.18: First-Continuation Dependency and Failure Audit
- Identified 5 immediate blockers called by upstream `kernel_bootstrap`:
  1. `vm_kernel_init()` (Zone and VM map allocator)
  2. `ipc_bootstrap()` (Mach IPC spaces, ports, rights)
  3. `PE_init_iokit()` (IOKit driver registry)
  4. `bsd_init()` (BSD process model, VFS, BSD syscalls)
  5. `launchd` ELF/Mach-O user-space execution infrastructure.

### Stage 271.19: Interrupt and DAIF Integration (Three Disjoint Profiles)
- Profile 1 (Historical Boot): `DAIF.I=1`, halts at WFI `0x2C14`, loops at `0x2C18`.
- Profile 2 (Architectural Interrupt): `DAIF.I=0`, AIC2 timer IRQ event `0x20000` popped, EOI acknowledged, clean return.
- Profile 3 (Context Transition): `DAIF.I=1`, stack pivoted to `0xfffffe0007044000`.

### Stage 271.20: Independent Architectural State Oracle
- Verified all CPU registers (`X0`, `X1`, `X2`, `SP`, `PC`, `EL`) against independently computed expected state.
- Verified target stack resides strictly outside guard frames.

### Stage 271.21: Adversarial Transition Mutations
- 20 distinct failure modes evaluated and verified:
  1. Missing `load_context` symbol.
  2. Wrong upstream source revision.
  3. Wrong destination symbol (address `0x10000` data page).
  4. Incorrect branch register relocation.
  5. Corrupted `X0` argument.
  6. Corrupted saved `LR`.
  7. Null stack pointer.
  8. Misaligned stack pointer.
  9. Guard-page stack pointer.
  10. Corrupted PSTATE mode.
  11. Premature interrupt unmasking.
  12. Null thread ownership.
  13. Invalid virtual core ID.
  14. Missing thread stack backing.
  15. Out-of-bounds unmapped PC.
  16. W+X memory security violation.
  17. Stripped/corrupted PAC tag.
  18. Host backdoor bypass.
  19. Misclassified synthetic continuation.
  20. Runtime instruction divergence from hashed binary.
- Detection Rate: 20/20 (100% correct failure attribution).

### Stage 271.22: Provenance and Maturity Audit
- Provenance Matrix:
  - Upstream Source: `xnu-10002.1.13` (Category E)
  - Historical Mach-O: `xnu_genuine_init.macho` (Category A/B)
  - Instruction Engine: WinIOS CPU / Memory C++20 models (Category C)
  - Adapted `load_context`: 6-instruction sequence (Category B)
  - Test Continuations: Synthetic harness code (Category D)
- Certified Level 3 retained; Level 4 rejected.

### Stage 271.23: Dual-Configuration CTest Regression
- Complete unfiltered CTest executed in both configurations:
  - `RelWithDebInfo`: **1,187 -> 1,192 tests (1,191 passed, 1 skipped `#383: inferno_integration_tests`, 0 failed, 100.0% pass rate)**.
  - `Debug`: **1,187 -> 1,192 tests (1,191 passed, 1 skipped `#383: inferno_integration_tests`, 0 failed, 100.0% pass rate)**.

### Stage 271.24: Final Artifacts, Cryptographic Manifest & Local Commit
- Deliverables generated under `artifacts/campaign-271/`:
  - `WINIOS_CAMPAIGN_271_FINAL_REPORT.md`
  - `WINIOS_CAMPAIGN_271_SHA256_MANIFEST.txt`
- Cryptographic SHA-256 manifest produced.
- Clean working tree verified. Local commit executed. Remote push strictly avoided.

---

## 4. Dual-Configuration Test Regression Verification (Stage 271.23)

| Configuration | Total Tests | Passed | Skipped | Failed | Pass Rate | Execution Time |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **RelWithDebInfo** | 1,192 | 1,191 | 1 (`#383`) | 0 | **100.0%** | 180.23 s |
| **Debug** | 1,192 | 1,191 | 1 (`#383`) | 0 | **100.0%** | 1122.32 s |

---

## 5. Provenance & Maturity Classification Matrix

| Subsystem / Milestone | Provenance Category | Implementation Artifact | Verified Status |
| :--- | :---: | :--- | :--- |
| Upstream Source Reference | Category E | `osfmk/kern/startup.c` (xnu-10002.1.13) | Cited & Audited |
| Early Boot Binary (`_start` -> WFI) | Category A/B | `embedded_xnu_image_210.inl` (`xnu_genuine_init.macho`) | Certified Level 3 |
| Guest Hardware MMU / AIC2 | Category C | `src/cpu/cpu.cpp`, `src/device/apple_aic.cpp` | Certified Level 3 |
| Architectural `load_context` Routine | Category B | 6-instruction native ARM64 adaptation (`0xD518D080`..) | Verified Execution |
| Test Harness Continuations | Category D | `stage271_execution_trace_and_continuation_tests.cpp` | Synthetic Harness |
| Genuine Upstream `kernel_bootstrap` | Category E | Unlinked in Mach-O binary | Blocked (Unlinked) |
| Mach IPC / User-Space Launch | Category E | Not implemented in guest runtime | Level 4 Rejected |

---

## 6. Campaign 271 Closeout Verification & Signoff

- **Baseline HEAD:** `54fb9bdab98402893e6df63c9d715db4aa8ca7ec`
- **Linear Ancestry:** Verified through Campaign 270, Campaign 269, Campaign 268, Campaign 267.
- **Repository Protections:** All invariant file hashes bit-exact; forbidden README nonexistent; Inferno isolated.
- **Platform Maturity Status:** **Certified Level 3 Retained; Level 4 Formally Rejected.**
