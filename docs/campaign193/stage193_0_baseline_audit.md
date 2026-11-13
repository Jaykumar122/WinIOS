# CAMPAIGN 193 — STAGE 193.0 BASELINE AUDIT AND ARCHITECTURAL INVENTORY

- **Campaign:** 193 (ARM64 Exception Architecture, EL0/EL1 Privilege Transitions & Synthetic Kernel Execution)
- **Stage:** 193.0
- **Baseline Git Commit:** `9ff79b3`
- **Host Platform:** Windows 11 x64 (MSVC C++20, `/W4 /WX /permissive-`)
- **Target Device Identity:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 `T8120`)
- **Status:** **COMPLETED — VERIFIED**

---

## 1. BASELINE REGRESSION AUDIT

Prior to modifying any source code for Campaign 193, full clean builds and complete CTest regressions were performed in both build configurations:

### Release Configuration
- **Build Status:** Clean (0 errors, 0 warnings).
- **CTest Results:** **348 passed, 1 skipped, 0 failed** (100% of runnable targets, 349 total).
- **Skipped Target:** `inferno_integration_tests` (requires explicit opt-in environment flag by design).
- **Total Test Execution Time:** ~125 seconds.

### Debug Configuration
- **Build Status:** Clean (0 errors, 0 warnings).
- **CTest Results:** **348 passed, 1 skipped, 0 failed** (100% of runnable targets, 349 total).
- **Skipped Target:** `inferno_integration_tests` (requires explicit opt-in environment flag by design).
- **Total Test Execution Time:** ~233 seconds.

### External Checkouts & Protected Files
- `D:\WinISO26\Inferno-Research\Inferno`: Clean, untouched, pinned at commit `cc4302a99167abec69b714cfd00c38caece7e7de`.
- `D:\WinISO26\WinIOS_README.md`: Untouched.
- `D:\WinISO26\WinIOS\docs\progress.md`: Untouched.
- `D:\WinISO26\WinISO26_README.md`: Does not exist (not created).

---

## 2. ARCHITECTURAL INVENTORY

### A. CPU Execution Loop & Privilege Representation
- **Source:** `src/cpu/cpu.h`, `src/cpu/cpu.cpp`.
- **Current Model:**
  - `current_el_`: Explicit 2-bit architectural execution level (`0` = EL0, `1` = EL1, `2` = EL2, `3` = EL3).
  - `sp_sel_`: SPSel bit selects between `SP_EL0` (`false` or EL0) and `SP_ELx` (`true` in EL1+).
  - `sp_el0_`, `sp_el1_`, `sp_el2_`, `sp_el3_`: Banked stack pointer storage.
  - `pstate_ss_`: Software Step process state bit.
  - `il_`: Illegal execution state bit.
  - `daif_`: 4-bit interrupt mask (D, A, I, F).
  - `nzcv_`: 4-bit condition flags (N, Z, C, V).
- **Findings & Gap Analysis:**
  - The CPU core internally supports `current_el_ == 0` and `current_el_ == 1`.
  - However, in userspace execution flows (`DarwinUserspaceEngine`), exceptions taken from EL0 (e.g. `SVC #0x80`) transition `current_el_` to 1, but are immediately intercepted by host C++ logic (`handle_svc`), which dispatches host handlers, sets PC to `ELR_EL1`, and forces `current_el_` back to 0.
  - No synthetic guest kernel code executes *guest instructions* at EL1 to service exceptions and issue an architectural `ERET` back to EL0.

### B. Exception Architecture & Vector Dispatch
- **Source:** `src/cpu/synchronous_exception.h`, `src/cpu/cpu.cpp`.
- **Current Model:**
  - Centralized `deliver_architectural_exception(const ArchitecturalException&)` in `src/cpu/cpu.cpp` lines 4380-4459.
  - Computes vector offsets relative to `VBAR_ELx` via `compute_vector_offset()` according to ARMv8-A D1.10:
    - Lower EL (AArch64): `+0x400` base section.
    - Current EL with SPx: `+0x200` base section.
    - Current EL with SP0: `+0x000` base section.
    - Synchronous offset: `+0x000`.
    - IRQ offset: `+0x080`.
    - FIQ offset: `+0x100`.
    - SError offset: `+0x180`.
  - Captures `SPSR_EL1` via `encode_spsr(nzcv_, daif_, current_el_, sp_sel_, il_, pstate_ss_)`.
  - Captures `ELR_EL1` from preferred return address.
  - Captures `ESR_EL1` and `FAR_EL1`.
  - Enforces DAIF masking (`daif_ = 0xF`) and switches `current_el_ = 1`, `sp_sel_ = true`.
- **Findings & Gap Analysis:**
  - The vector calculation and register capture logic is architecturally compliant with ARMv8-A.
  - Needs verification that `VBAR_EL1` lower 11 bits are strictly masked to 0 (2KB alignment requirement).
  - Needs end-to-end guest tests where guest code actually resides at `VBAR_EL1 + offset` and executes handler instructions.

### C. Exception Return (`ERET`)
- **Source:** `src/cpu/cpu.cpp` lines 7777-7835.
- **Current Model:**
  - Opcode: `0xD69F03E0`.
  - Privilege check: Prohibited at EL0 (raises EC 0x00 `UndefinedInstruction` when `architectural_exceptions_` is enabled).
  - SPSR validation: Checks mode field `M[4:0]`. Only `0x00` (EL0t), `0x04` (EL1t), `0x05` (EL1h) are valid return targets from EL1.
  - Illegal mode handling: Sets `il_ = true` (PSTATE.IL), which forces an Illegal Execution State exception on the next instruction fetch.
  - Restores `nzcv_`, `daif_`, `il_`, `pstate_ss_`, `current_el_`, `sp_sel_`, and sets `program_counter_ = ELR_ELx`.
- **Findings & Gap Analysis:**
  - Complete architectural validation exists; needs focused guest integration testing showing an EL1 guest handler successfully returning to an interrupted EL0 guest program.

### D. System Registers & Privilege Enforcement
- **Source:** `src/cpu/sysreg.h`, `src/cpu/sysreg.cpp`.
- **Current Model:**
  - Over 100 system registers indexed by architectural key `(o0 << 14) | (op1 << 11) | (CRn << 7) | (CRm << 3) | op2`.
  - `SysRegDescriptor` defines `min_el`, read/write permissions, reset values, and RES0/RES1 masks.
  - `check_privilege()` checks access level against `current_el_`. If `current_el_ < min_el`, returns `PrivilegeResult::UndefinedException` (EC 0x00).
- **Findings & Gap Analysis:**
  - System registers (`CurrentEL`, `VBAR_EL1`, `ESR_EL1`, `ELR_EL1`, `SPSR_EL1`, `FAR_EL1`, `SCTLR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`, `TCR_EL1`, `MAIR_EL1`, `TPIDR_EL0`, `TPIDR_EL1`, `SP_EL0`, `SP_EL1`) are defined and privileged.
  - Need test coverage confirming EL0 access attempts to `VBAR_EL1`, `SCTLR_EL1`, etc. trap with EC 0x00, while EL1 accesses succeed.

### E. MMU, Translation & Page Table Walks
- **Source:** `src/cpu/mmu.h`, `src/cpu/mmu.cpp`.
- **Current Model:**
  - Supports 4KB, 16KB, and 64KB granules via `TG0` / `TG1` in `TCR_EL1`.
  - Validates Level 0, 1, 2, 3 Table, Block, and Page descriptors.
  - Checks permission bits: `AP[2:1]`, `UXN`, `PXN`.
  - Generates `TranslationFault` with FSC for AddressSize, Translation, AccessFlag, Permission, ExternalWalk, and Alignment.
  - Implements software TLB (64 entries) with ASID/VMID tagging, invalidation functions (`TLBI VMALLE1`, `VAE1`, `ASIDE1`, `VAA1`).
- **Findings & Gap Analysis:**
  - Page-table walk and permission checks correctly distinguish EL0 and EL1 access privileges.
  - Need end-to-end guest verification that accessing EL1-only memory from EL0 generates a Permission Fault delivered as a Data Abort to EL1, while EL1 access succeeds.

### F. GIC & Generic Timers
- **Source:** `src/cpu/gicv3.h`, `src/cpu/gicv3.cpp`, `src/cpu/virtual_time.h`.
- **Current Model:**
  - Full GICv3 Distributor and CPU interface emulation (`ICC_PMR_EL1`, `ICC_IAR1_EL1`, `ICC_EOIR1_EL1`, `ICC_HPPIR1_EL1`).
  - Virtual time counter and generic timers (Physical CNTP, Virtual CNTV).
  - Assertion of PPI 27 / PPI 30 into GIC triggers `raise_asynchronous_exception(false, intid, ...)`.
- **Findings & Gap Analysis:**
  - Needs verification of the entire interrupt lifecycle driven by guest instructions: Timer expires -> GIC asserts IRQ -> CPU takes IRQ exception to EL1 vector `VBAR_EL1 + 0x480` -> Guest handler reads `ICC_IAR1_EL1`, clears timer, writes `ICC_EOIR1_EL1`, and issues `ERET` back to EL0.

---

## 3. IDENTIFIED RISKS & MITIGATION PLAN

| Risk | Description | Mitigation Strategy |
|---|---|---|
| **Host Callback Masquerading** | Using host C++ callbacks to pretend kernel execution occurred. | All kernel execution in Stages 193.16–193.18 will be genuine ARM64 machine code executing in the EL1 guest CPU via independently compiled binary artifacts (`synthetic_el1_kernel`). |
| **Regression in EL0 Darwin Apps** | Enhancing exception handling breaks existing Darwin userspace syscall paths. | Retain `DarwinUserspaceEngine` syscall dispatch behavior while adding pure guest architectural exception tests in dedicated suites. |
| **Evidence Overclaiming** | Confusing synthetic kernel execution with genuine Apple XNU boot. | Explicitly enforce the execution categories (`HOST_MODEL`, `GUEST_EL0`, `GUEST_EL1`, `GUEST_EXCEPTION`, `GUEST_INTERRUPT`, `SYNTHETIC_KERNEL`, `APPLE_KERNEL`). No claims of Apple kernel boot without genuine XNU evidence. |

---

## 4. STAGE 193.0 ACCEPTANCE VERDICT

- Baseline audit completed: **PASS**.
- CTest regressions verified: **348 passed, 1 skipped, 0 failed in both Release and Debug**.
- Repository safety confirmed: **Zero uncommitted changes, protected files untouched, pinned Inferno untouched**.
- Stage 193.0 is **COMPLETE**.
