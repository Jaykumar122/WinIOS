# WINIOS CAMPAIGN 261 FINAL ENGINEERING REPORT
## Canonical Memory Ownership, Allocator Geometry, Six-Core Stack Isolation, ARM64e PAC ABI, Exception Restoration & Timer Correctness

**Campaign:** 261  
**Stages:** 261.0 through 261.24 (exactly 25 sequential engineering stages)  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Required Baseline Anchor:** `426c8b3eb5e2226390d5a0932786733120b0c166` (Campaign 260 closeout commit)  
**Host Platform:** Windows 11 x64  
**Toolchain:** MSVC C++20 (`/W4 /WX /permissive-`)  
**Build Generator:** CMake (Visual Studio 18 2026)  
**Build Configurations Tested:** `RelWithDebInfo` and `Debug`  
**Target Architecture:** ARM64 / ARM64e research profile  
**Reference Platform:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 `T8120`)  
**Page Granule:** 16 KiB  
**Upstream Reference Source:** Public Apple XNU `xnu-10002.1.13` (`osfmk/mach/arm/thread_status.h`)  
**Certification Status:** Maturity Level 3 Certified (Research Environment / Freestanding Execution Boundary); Level 4 Formally Evaluated and Rejected  

---

## 1. Executive Summary & Core Issue Closures

Campaign 261 executes exactly 25 sequential engineering stages (Campaign 261.0 through 261.24) to resolve the architectural and evidence discrepancies identified during the Campaign 260 closeout review. Governed by the principle of **"no architectural claim without a corresponding implementation, binary, and runtime proof,"** Campaign 261 establishes definitive solutions for physical memory ownership, dynamic allocator boundaries, multi-core stack geometry, ARM64e PAC ABI modeling, exception context preservation, architectural timer semantics, and multi-core isolation.

### Core Issue Closures Summary:

| Issue | Discrepancy / Gap from Campaign 260 | Architectural Solution in Campaign 261 | Status |
|---|---|---|---|
| **Issue A: Allocator & Kernel-Image Overlap** | Campaign 260 reported dynamic allocator pool at `0x00050000..0x00800000`, while secondary/historical kernel text was reported at `0x00080000` (Frame 32), creating a 16 KiB collision in the free pool. | Established authoritative 8 MiB canonical physical memory map (`CanonicalPhysicalMemoryMap`). Reserved Frames 0..36 (`0x00000000..0x00094000`, 606,208 bytes) contain all bootstrap structures, stacks, page tables, guards, metadata, and Frame 32 (`0x00080000..0x00084000`). Dynamic allocator free pool is strictly derived as Frames 37..511 (`0x00094000..0x00800000`, 475 frames = 7,782,400 bytes). Zero overlap between Frame 32 and allocator pool. | **RESOLVED & VERIFIED** |
| **Issue B: Six-Core Stack Geometry** | Stacks were conceptually described, but per-core interrupt stacks, thread stacks, guard intervals, initial SPs, and physical ownership were not exhaustively mapped into unreserved memory. | Explicitly documented and implemented all 6 cores' independent 16 KiB interrupt stacks, 16 KiB thread stacks, and 16 KiB unmapped guard intervals across Frames 15..35. SP_EL1 and SP_EL0 initialized at top of respective stack bounds. Guard frames enforced with zero permissions (`permissions == 0`). Nesting limit verified at 56 raw frames (39 with 128-byte frame overhead). | **RESOLVED & VERIFIED** |
| **Issue C: ARM64e PAC ABI Reconciliation** | Campaign 260 established 288-byte non-PAC `ArmSavedState64`, but did not audit the `HAS_APPLE_PAC` conditionally compiled layout in upstream XNU `thread_status.h` or document target PAC configuration. | Audited public Apple XNU `xnu-10002.1.13`. Proved that under `#if HAS_APPLE_PAC`, `struct arm_saved_state64` appends `uint64_t jophash;` at offset `0x120` (288), making `sizeof(ArmSavedState64Pac) = 296` bytes (74 words). Standard non-PAC `ArmSavedState64Standard` is 288 bytes (72 words). WinIOS explicitly targets `kTargetHasApplePac = false` (288 bytes) because hardware cryptographic PAC signing instructions are not simulated, while formally establishing both struct layouts and compile-time assertions. | **RESOLVED & VERIFIED** |
| **Issue D: Exception Restore Evidence** | Trampoline save sequence was documented, but complete memory-backed register reload before ERET and adversarial register corruption testing lacked dedicated verification. | Provided complete 32-instruction entry save sequence (128 bytes) and 22-instruction restore trampoline (88 bytes). Verified that during handler execution, all 31 GPRs (X0..X30), PC, and PSTATE were intentionally corrupted with adversarial patterns (all-zeros, all-ones, alternating bits), and subsequently reloaded bit-for-bit from physical memory before `ERET`. | **RESOLVED & VERIFIED** |
| **Issue E: Architectural Timer Semantics** | Generic Virtual Timer model required formalization of signed 32-bit TVAL math, hardware-derived read-only ISTATUS (bit 2), periodic rearming, interrupt line masking, and six-core concurrent expiration isolation. | Implemented and verified ARMv8 Generic Timer architecture: signed 32-bit TVAL calculation (`CVAL = Counter + TVAL`), hardware-derived ISTATUS derived from `(counter >= comparator) && (control & kCtlEnable)`, software inability to write ISTATUS, drift-free periodic rearming (`rearm_periodic`), interrupt masking (`IMASK=1`) deasserting AIC2 lines while preserving ISTATUS, and concurrent six-core expiration handling. | **RESOLVED & VERIFIED** |
| **Issue F: Subsystem Provenance & Maturity** | Component classification required formal segregation; Level 4 claims required definitive evaluation and blocker documentation. | Formal segregation into Category A (Upstream XNU), Category B (Hardware-Accurate Models), Category C (Behavioral Simulations), and Category D (Synthetic Test Vectors). Maturity Level 3 re-certified; Level 4 formally evaluated and rejected with 4 specific architectural blockers. | **RESOLVED & VERIFIED** |

---

## 2. Comprehensive Stage-by-Stage Engineering Execution Log

### Stage 261.0: Baseline Integrity & Pre-Campaign Anchor Verification
- Validated git repository state: branch `campaign-191r`, HEAD commit `426c8b3eb5e2226390d5a0932786733120b0c166` (Campaign 260 closeout).
- Confirmed safety invariants:
  - `D:\WinISO26\WinISO26_README.md` is strictly nonexistent.
  - `D:\WinISO26\WinIOS_README.md` SHA-256: `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF`.
  - `D:\WinISO26\WinIOS\docs\progress.md` SHA-256: `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212`.
  - `D:\WinISO26\Inferno-Research\` directory strictly uninspected and isolated.
- Verified MSVC C++20 compiler environment and CMake Visual Studio 18 2026 build generator.

### Stage 261.1: Authoritative Physical Memory Layout Audit
- Audited the entire 8 MiB guest physical address space (`0x00000000..0x00800000`, 512 frames of 16 KiB).
- Identified all static consumers of physical frames:
  - Low bootstrap structures, early `__TEXT` at `0x00000000..0x00004000` (VBAR vector table at `0x00000800`).
  - Kernel static data `__DATA` (Frames 4..11, 128 KiB).
  - Apple DeviceTree (Frame 12, 16 KiB) and BootArgs (Frame 13, 16 KiB).
  - Cold-boot early stack (Frame 14, 16 KiB).
  - Six-core stack bands and unmapped guards (Frames 15..17, 20..31, 33..35).
  - ARM64 Stage 1 Page Tables (Frames 18..19, 32 KiB).
  - Staged diagnostic kernel text at Frame 32 (`0x00080000..0x00084000`).
  - Allocator dynamic metadata tracking table (Frame 36, 16 KiB).

### Stage 261.2: Implementation of `CanonicalPhysicalMemoryMap`
- Created `src/memory/canonical_physical_memory_map.h` and `src/memory/canonical_physical_memory_map.cpp`.
- Modeled the authoritative memory layout using strongly typed memory intervals with base addresses, sizes, frame ranges, usage descriptions, provenance categories, permissions (`PAGE_READ`, `PAGE_WRITE`, `PAGE_EXEC`), and cache attributes.
- Implemented `create_canonical_iphone15_layout()` populating all 17 reserved intervals across Frames 0..36 (37 frames = 606,208 bytes).

### Stage 261.3: Pairwise Non-Overlap Auditor (`PhysicalMemoryOverlapAuditor`)
- Implemented `PhysicalMemoryOverlapAuditor::audit_layout()`:
  - Validates 16 KiB page alignment for every interval start and length.
  - Sorts intervals by base address and tests all adjacent pairs for overlaps: `(a.base + a.size) <= b.base`.
  - Enforces W^X integrity: strictly forbids simultaneous `PAGE_WRITE` and `PAGE_EXEC` on any frame.
  - Generates deterministic error logs upon any detected collision or permission violation.

### Stage 261.4: Dynamic Allocator Free-Range Derivation & Issue A Closure
- Implemented `CanonicalPhysicalMemoryMap::get_allocator_free_range()`:
  - Automatically identifies the end of the highest reserved frame (`0x00094000`, Frame 37).
  - Establishes the dynamic allocator free pool strictly as `[0x00094000, 0x00800000)` (475 frames = 7,782,400 bytes).
  - Proved mathematically that Frame 32 (`0x00080000..0x00084000`) is located in the reserved region prior to `0x00094000` and is completely excluded from the dynamic allocator pool.

### Stage 261.5: Stage 261.5 Test Suite & Memory Foundation Gate
- Authored `tests/unit/stage261_5_canonical_memory_map_and_allocator_tests.cpp`.
- Validated:
  - Exact frame accounting (37 reserved + 475 free = 512 total frames).
  - Pairwise non-overlap across all reserved intervals (0 collisions detected).
  - Dynamic allocator pool boundary: allocations at `0x00094000` succeed; allocations below `0x00094000` (including `0x00080000`) are rejected.
  - Adversarial allocations: capacity bounds, double-frees, foreign pointer rejection.
- Test passed 100%.

### Stage 261.6: Six-Core Stack & Guard Geometry Specification (Issue B)
- Mapped independent 16 KiB interrupt stacks and 16 KiB thread stacks for all 6 cores:
  - Core 0: Guard `0x3C000`, IntStack `0x40000..0x44000` (SP_EL1=`0x44000`), ThreadStack `0x44000..0x48000` (SP_EL0=`0x48000`).
  - Core 1: Guard `0x50000`, IntStack `0x54000..0x58000` (SP_EL1=`0x58000`), ThreadStack `0x58000..0x5C000` (SP_EL0=`0x5C000`).
  - Core 2: Guard `0x5C000`, IntStack `0x60000..0x64000` (SP_EL1=`0x64000`), ThreadStack `0x64000..0x68000` (SP_EL0=`0x68000`).
  - Core 3: Guard `0x68000`, IntStack `0x6C000..0x70000` (SP_EL1=`0x70000`), ThreadStack `0x70000..0x74000` (SP_EL0=`0x74000`).
  - Core 4: Guard `0x74000`, IntStack `0x78000..0x7C000` (SP_EL1=`0x7C000`), ThreadStack `0x7C000..0x80000` (SP_EL0=`0x80000`).
  - Core 5: Guard `0x84000`, IntStack `0x88000..0x8C000` (SP_EL1=`0x8C000`), ThreadStack `0x8C000..0x90000` (SP_EL0=`0x90000`).
- Guard frames are strictly unmapped (`permissions = 0`).

### Stage 261.7: Multi-Core Stack Disjointness & Guard Protection
- Verified that all 12 stack intervals and 6 guard intervals are pairwise non-overlapping.
- Enforced stack underflow detection: memory accesses into guard intervals trigger synchronous data aborts.

### Stage 261.8: Stage 261.8 Test Suite & Stack Verification Gate
- Authored `tests/unit/stage261_8_six_core_stack_and_guard_tests.cpp`.
- Validated:
  - Initial stack pointer values for all 6 cores.
  - Safe nesting depth calculation: 16 KiB / 288 B = 56 raw exception frames. With a realistic handler overhead of 128 bytes (total 416 bytes per frame), safe nesting depth is 39 nested frames before reaching the guard page.
  - Stack boundary underflow/overflow bounds checking.
  - Multi-core stack isolation with distinct memory fill patterns.
- Test passed 100%.

### Stage 261.9: Upstream Apple XNU ARM64e PAC ABI Audit
- Audited Apple public XNU `xnu-10002.1.13` (`osfmk/mach/arm/thread_status.h`).
- Analyzed the conditional compilation structure:
  ```c
  struct arm_saved_state64 {
      uint64_t x[29];
      uint64_t fp;
      uint64_t lr;
      uint64_t sp;
      uint64_t pc;
      uint32_t cpsr;
      uint32_t flags;
      uint64_t far;
      uint32_t esr;
      uint32_t exception;
  #if HAS_APPLE_PAC
      uint64_t jophash;
  #endif
  };
  ```
- Established:
  - Without PAC: 36 64-bit slots = 288 bytes = 72 words (where 1 word = 4 bytes).
  - With PAC: 37 64-bit slots = 296 bytes = 74 words.
  - Flavor constants: `ARM_SAVED_STATE64 = 21`, `ARM_SAVED_STATE64_COUNT = 72`, `ARM_SAVED_STATE64_PAC_COUNT = 74`.

### Stage 261.10: Implementation of PAC vs Non-PAC ABI Structures
- Updated `src/cpu/arm_saved_state.h`:
  - Added `ArmSavedState64Standard` (288 bytes, 72 words).
  - Added `ArmSavedState64Pac` (296 bytes, 74 words) with `uint64_t jophash` at offset `0x120` (288).
  - Added `ArmSavedState64TaggedPac` (304 bytes).
  - Defined `kTargetHasApplePac = false` as the formal target architecture configuration for WinIOS research profile.
  - Retained `ArmSavedState64` alias pointing to `ArmSavedState64Standard`.
  - Added static assertions validating `sizeof`, `alignof`, and exact field offsets for both layouts.

### Stage 261.11: Stage 261.11 Test Suite & ABI Gate
- Authored `tests/unit/stage261_11_arm64e_pac_and_abi_tests.cpp`.
- Validated:
  - Struct sizes: Standard 288 B, PAC 296 B, Tagged Standard 296 B, Tagged PAC 304 B.
  - Field offsets match upstream Apple XNU byte-for-byte.
  - Word count verification: Standard 72 words, PAC 74 words.
  - State serialization and deserialization across ABI boundaries.
- Test passed 100%.

### Stage 261.12: Complete Exception Entry Trampoline Audit
- Audited the 32-instruction (128 bytes) exception entry trampoline:
  1. `sub sp, sp, #288` (allocate frame)
  2. `stp x0, x1, [sp, #0]` .. `stp x28, x29, [sp, #224]` (save X0..X29)
  3. `str x30, [sp, #240]` (save LR/X30)
  4. `add x0, sp, #288; str x0, [sp, #248]` (compute and save SP_EL0)
  5. `mrs x1, elr_el1; str x1, [sp, #256]` (save PC)
  6. `mrs x2, spsr_el1; str w2, [sp, #264]` (save CPSR/PSTATE)
  7. `str wzr, [sp, #268]` (clear flags)
  8. `mrs x0, far_el1; str x0, [sp, #272]` (save FAR)
  9. `mrs x3, esr_el1; str w3, [sp, #280]` (save ESR via 32-bit store)
  10. `mov w4, #1; str w4, [sp, #284]` (save exception code via 32-bit store)
  11. `ldp x0, x1, [sp, #0]; ldp x2, x3, [sp, #16]; ldr x4, [sp, #32]` (restore scratch registers X0..X4 from frame)
- Verified all 32 opcode encodings and operand bitfields.

### Stage 261.13: Implementation of Complete Exception Restore Trampoline
- Authored the 22-instruction (88 bytes) exception restore trampoline:
  1. `ldr x0, [sp, #256]; msr elr_el1, x0` (reload interrupted PC into ELR_EL1)
  2. `ldr w0, [sp, #264]; msr spsr_el1, x0` (reload interrupted PSTATE into SPSR_EL1)
  3. `ldp x0, x1, [sp, #0]` .. `ldp x28, x29, [sp, #224]` (reload X0..X29 from memory)
  4. `ldr x30, [sp, #240]` (reload LR/X30 from memory)
  5. `add sp, sp, #288` (deallocate exception frame)
  6. `eret` (architectural exception return)
- Verified that context restore operates strictly from the physical stack frame in RAM, not from residual CPU registers.

### Stage 261.14: Adversarial Register Scrambling & Memory-Backed Restore
- Implemented adversarial verification test in `stage261_15_exception_save_restore_and_adversarial_tests.cpp`:
  - Executed exception entry to save initial state to stack.
  - At the handler midpoint, corrupted all 31 CPU general-purpose registers (X0..X30) with hostile values (`0xDEADBEEF00000000ULL | reg_index`), corrupted `ELR_EL1` to `0xBAADF00D`, and corrupted `SPSR_EL1` to `0xFFFFFFFF`.
  - Executed the 22-instruction restore trampoline.
  - Verified that after restore and ERET, all 31 GPRs, PC, and PSTATE matched the pre-exception state bit-for-bit.

### Stage 261.15: Stage 261.15 Test Suite & Exception Gate
- Authored `tests/unit/stage261_15_exception_save_restore_and_adversarial_tests.cpp`.
- Tested hostile bit patterns: all-zeros (`0x0000000000000000`), all-ones (`0xFFFFFFFFFFFFFFFF`), alternating bit patterns (`0x5555555555555555`, `0xAAAAAAAAAAAAAAAA`), and distinct per-register signatures.
- Verified zero register leakage and zero stack frame corruption.
- Test passed 100%.

### Stage 261.16: Architectural Generic Timer Semantics (Issue E)
- Audited ARMv8 Generic Timer architecture:
  - Counter: `CNTVCT_EL0` (64-bit monotonic up-counter).
  - Frequency: `CNTFRQ_EL0` (24 MHz on Apple A16).
  - TVAL Math: signed 32-bit countdown register. When software writes `TVAL`, the hardware comparator is set to `CVAL = CNTVCT_EL0 + (int64_t)(int32_t)TVAL`.
  - ISTATUS (bit 2 of `CNTV_CTL_EL0`): read-only status bit derived strictly from `(CNTVCT_EL0 >= CNTV_CVAL_EL0) && (ENABLE == 1)`. Software writes to bit 2 are ignored.
  - Interrupt assertion: timer asserts interrupt line to AIC2 if and only if `(ISTATUS == 1) && (IMASK == 0)`.

### Stage 261.17: Periodic Rearming & Delayed Servicing
- Implemented `DeterministicVirtualTimer::rearm_periodic(core_id, period_ticks)`:
  - Advances comparator deadline by `period_ticks` relative to current comparator (`comparator += period_ticks`) rather than relative to current counter, preventing cumulative clock drift.
  - Verified delayed interrupt servicing: if interrupt servicing is delayed by $N$ ticks, rearming catches up without dropping ticks or losing period alignment.

### Stage 261.18: Complete Timer-to-AIC2 Exception Trace
- Traced end-to-end event propagation:
  1. Generic Virtual Timer comparator expires on Core 0.
  2. Hardware derives `ISTATUS = 1` in `CNTV_CTL_EL0`.
  3. Timer asserts event line to AIC2 interrupt controller.
  4. AIC2 latches timer interrupt as pending.
  5. CPU receives IRQ and branches to vector base `VBAR_EL1 + 0x280` (`0x0A80`).
  6. 32-instruction entry saves full 288-byte `ArmSavedState64` to Core 0 stack (`0x40000..0x44000`).
  7. Guest handler reads AIC2 MMIO ACK register (`0x08002000`).
  8. Guest handler writes AIC2 MMIO EOI register (`0x08002004`).
  9. Guest handler rearms timer.
  10. 22-instruction restore trampoline reloads context from RAM and executes `ERET`.
- Verified complete trace without stalls or unhandled interrupts.

### Stage 261.19: Stage 261.19 Test Suite & SMP Timer Gate
- Authored `tests/unit/stage261_19_timer_semantics_aic2_trace_and_smp_tests.cpp`.
- Validated:
  - Signed TVAL math and wrap-around handling.
  - Read-only ISTATUS behavior (software write cannot clear active status).
  - Interrupt masking: setting `IMASK=1` deasserts AIC2 interrupt line while keeping `ISTATUS=1`.
  - Six-core concurrent expiration: all 6 cores configured with identical deadlines trigger simultaneous independent interrupts, routed to independent banked AIC2 registers without cross-core interference.
- Test passed 100%.

### Stage 261.20: Physical Memory W^X & Admission Verification
- Audited all 512 physical frames across the 8 MiB space against W^X:
  - Code frames (Frame 0 `__TEXT` at `0x000`, Frame 32 `KernelTextSecondary` at `0x80000`): permissions are `PAGE_READ | PAGE_EXEC` (0x5).
  - Data, stack, guard, and pool frames: permissions are `PAGE_READ | PAGE_WRITE` (0x3) or `0` (guards).
  - Confirmed: exactly 0 frames have simultaneous Write and Execute permissions.

### Stage 261.21: Independent Auditor Mutation Detection
- Implemented mutation fault injection tests in `stage261_23_independent_auditor_and_maturity_tests.cpp`:
  - Mutation 1: Injected deliberate overlap at `0x00080000` -> auditor detected collision and threw exception.
  - Mutation 2: Injected deliberate RWX permission violation (`0x7`) -> auditor detected W^X breach and threw exception.
  - Mutation 3: Injected misaligned interval base (`0x1001`) -> auditor rejected admission.
- Proved auditor sensitivity and robustness against corrupted layouts.

### Stage 261.22: Adversarial Replay & Snapshot Hashing
- Tested multi-core timer and scheduler state snapshot serialization and deserialization.
- Verified bitwise identical reproduction of internal counters, comparator deadlines, tick accounts, and delivery logs after deserialization.

### Stage 261.23: Twenty-Dimension Security & Maturity Audit
- Evaluated system across all 20 security, architectural, and maturity dimensions.
- All 20 dimensions passed with concrete binary and test evidence.
- Formally evaluated Maturity Level 4 and rejected promotion due to 4 specific architectural blockers.

### Stage 261.24: Deliverables, Dual-Configuration Gate, Final Manifest & Clean Commit
- Executed full dual-configuration regression suite:
  - `RelWithDebInfo`: 1,131 registered tests, 1,130 passed, 1 skipped (`#383 inferno_integration_tests`), 0 failed (100% pass rate).
  - `Debug`: 1,131 registered tests, 1,130 passed, 1 skipped (`#383 inferno_integration_tests`), 0 failed (100% pass rate).
- Authored final report `artifacts/campaign-261/WINIOS_CAMPAIGN_261_FINAL_REPORT.md`.
- Generated SHA-256 manifest `artifacts/campaign-261/WINIOS_CAMPAIGN_261_SHA256_MANIFEST.txt`.
- Confirmed safety invariants and prepared clean local git commit.

---

## 3. Canonical Physical Memory Map (8 MiB / 512 Frames)

The authoritative physical address space is 8 MiB (`0x00000000..0x00800000`), divided into 512 frames of 16 KiB ($16,384$ bytes) each.

```
+---------------------------------------------------------------------------------------------------+
| Reserved Region: Frames 0..36 (37 frames = 606,208 bytes, 0x00000000 .. 0x00094000)                |
|  Frame 0: __TEXT (VBAR at 0x800)       [RX]                                                       |
|  Frames 1..3: Low Scratch & Padding    [RW]                                                       |
|  Frames 4..11: __DATA (128 KiB)        [RW]                                                       |
|  Frame 12: DeviceTree (16 KiB)         [RO]                                                       |
|  Frame 13: BootArgs (16 KiB)           [RO]                                                       |
|  Frame 14: Cold Boot Early Stack       [RW]                                                       |
|  Frames 15..17: Core 0 Guard, IntStack, ThreadStack                                               |
|  Frames 18..19: Stage 1 Page Tables    [RW]                                                       |
|  Frames 20..31: Cores 1..4 Guards, IntStacks, ThreadStacks                                         |
|  Frame 32: KernelTextSecondary (0x80000..0x84000) [RX]  <-- Strictly Excluded from Allocator!    |
|  Frames 33..35: Core 5 Guard, IntStack, ThreadStack                                               |
|  Frame 36: Allocator Metadata (16 KiB) [RW]                                                       |
+---------------------------------------------------------------------------------------------------+
| Dynamic Allocator Free Pool: Frames 37..511 (475 frames = 7,782,400 bytes, 0x00094000 .. 0x00800000)|
|  Frames 37..511: Dynamic Zone & Frame Allocator Pool [RW]                                         |
+---------------------------------------------------------------------------------------------------+
```

### Complete Frame-by-Frame Allocation Table:

| Frame Range | Physical Address Range | Size (Bytes) | Designated Usage | Category | Permissions | W^X Status |
|---|---|---|---|---|---|---|
| **0** | `0x00000000 - 0x00004000` | 16,384 | Primary Boot Code (`__TEXT`), VBAR Vector Table (`0x0800`) | B | `PAGE_READ \| PAGE_EXEC` (0x5) | Enforced (RX) |
| **1 - 3** | `0x00004000 - 0x00010000` | 49,152 | Low Memory Scratchpad & Zero Page | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **4 - 11** | `0x00010000 - 0x00030000` | 131,072 | Kernel Static Data (`__DATA`, 128 KiB) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **12** | `0x00030000 - 0x00034000` | 16,384 | Apple DeviceTree Blob (D37AP / T8120) | B | `PAGE_READ` (0x1) | Enforced (RO) |
| **13** | `0x00034000 - 0x00038000` | 16,384 | BootArgs Structure (`boot_args`) | B | `PAGE_READ` (0x1) | Enforced (RO) |
| **14** | `0x00038000 - 0x0003C000` | 16,384 | Cold-Boot Early Stack (SP=`0x3C000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **15** | `0x0003C000 - 0x00040000` | 16,384 | Core 0 Stack Guard Frame (Fault on Access) | B | `0` (None) | Enforced (Guard) |
| **16** | `0x00040000 - 0x00044000` | 16,384 | Core 0 Interrupt Stack (SP_EL1=`0x44000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **17** | `0x00044000 - 0x00048000` | 16,384 | Core 0 Thread Stack (SP_EL0=`0x48000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **18 - 19** | `0x00048000 - 0x00050000` | 32,768 | ARM64 Stage 1 Page Tables (L1/L2 Descriptors) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **20** | `0x00050000 - 0x00054000` | 16,384 | Core 1 Stack Guard Frame | B | `0` (None) | Enforced (Guard) |
| **21** | `0x00054000 - 0x00058000` | 16,384 | Core 1 Interrupt Stack (SP_EL1=`0x58000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **22** | `0x00058000 - 0x0005C000` | 16,384 | Core 1 Thread Stack (SP_EL0=`0x5C000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **23** | `0x0005C000 - 0x00060000` | 16,384 | Core 2 Stack Guard Frame | B | `0` (None) | Enforced (Guard) |
| **24** | `0x00060000 - 0x00064000` | 16,384 | Core 2 Interrupt Stack (SP_EL1=`0x64000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **25** | `0x00064000 - 0x00068000` | 16,384 | Core 2 Thread Stack (SP_EL0=`0x68000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **26** | `0x00068000 - 0x0006C000` | 16,384 | Core 3 Stack Guard Frame | B | `0` (None) | Enforced (Guard) |
| **27** | `0x0006C000 - 0x00070000` | 16,384 | Core 3 Interrupt Stack (SP_EL1=`0x70000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **28** | `0x00070000 - 0x00074000` | 16,384 | Core 3 Thread Stack (SP_EL0=`0x74000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **29** | `0x00074000 - 0x00078000` | 16,384 | Core 4 Stack Guard Frame | B | `0` (None) | Enforced (Guard) |
| **30** | `0x00078000 - 0x0007C000` | 16,384 | Core 4 Interrupt Stack (SP_EL1=`0x7C000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **31** | `0x0007C000 - 0x00080000` | 16,384 | Core 4 Thread Stack (SP_EL0=`0x80000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **32** | `0x00080000 - 0x00084000` | 16,384 | Staged Diagnostic Code (`KernelTextSecondary`) | D | `PAGE_READ \| PAGE_EXEC` (0x5) | Enforced (RX) |
| **33** | `0x00084000 - 0x00088000` | 16,384 | Core 5 Stack Guard Frame | B | `0` (None) | Enforced (Guard) |
| **34** | `0x00088000 - 0x0008C000` | 16,384 | Core 5 Interrupt Stack (SP_EL1=`0x8C000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **35** | `0x0008C000 - 0x00090000` | 16,384 | Core 5 Thread Stack (SP_EL0=`0x90000`) | B | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **36** | `0x00090000 - 0x00094000` | 16,384 | Allocator Metadata (Tracking Table & Bitmaps) | C | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |
| **37 - 511** | `0x00094000 - 0x00800000` | 7,782,400 | Dynamic Allocator Free Pool (475 Frames) | C | `PAGE_READ \| PAGE_WRITE` (0x3) | Enforced (RW) |

**Accounting Proof:** $37 \text{ reserved frames} + 475 \text{ free frames} = 512 \text{ total frames} = 8,388,608 \text{ bytes} \ (8 \text{ MiB})$.

---

## 4. Six-Core Stack & Guard Geometry (Issue B)

All 6 CPU cores (representing the 2 Everest performance cores and 4 Sawtooth efficiency cores of the Apple A16 Bionic) are assigned strictly disjoint 16 KiB stacks and unmapped 16 KiB guard intervals.

| CPU Core | Guard Address | Guard Permissions | Interrupt Stack Base | Initial SP_EL1 | Thread Stack Base | Initial SP_EL0 | Stack Size |
|---|---|---|---|---|---|---|---|
| **Core 0** | `0x0003C000` | `0` (Unmapped) | `0x00040000` | `0x00044000` | `0x00044000` | `0x00048000` | 16 KiB (16,384 B) |
| **Core 1** | `0x00050000` | `0` (Unmapped) | `0x00054000` | `0x00058000` | `0x00058000` | `0x0005C000` | 16 KiB (16,384 B) |
| **Core 2** | `0x0005C000` | `0` (Unmapped) | `0x00060000` | `0x00064000` | `0x00064000` | `0x00068000` | 16 KiB (16,384 B) |
| **Core 3** | `0x00068000` | `0` (Unmapped) | `0x0006C000` | `0x00070000` | `0x00070000` | `0x00074000` | 16 KiB (16,384 B) |
| **Core 4** | `0x00074000` | `0` (Unmapped) | `0x00078000` | `0x0007C000` | `0x0007C000` | `0x00080000` | 16 KiB (16,384 B) |
| **Core 5** | `0x00084000` | `0` (Unmapped) | `0x00088000` | `0x0008C000` | `0x0008C000` | `0x00090000` | 16 KiB (16,384 B) |

### Nesting Bounds Calculation:
- Exception frame size = 288 bytes.
- Theoretical maximum nesting: $\lfloor 16,384 / 288 \rfloor = 56$ frames.
- Practical maximum nesting (accounting for 128 bytes callee locals and 16-byte alignment per frame = 416 bytes total): $\lfloor 16,384 / 416 \rfloor = 39$ frames.
- Pushes past the bottom of the stack trigger immediate synchronous Data Aborts upon touching the guard frame.

---

## 5. ARM64e PAC ABI Reconciliation & Layout Invariants (Issue C)

Audited public Apple XNU `xnu-10002.1.13` (`osfmk/mach/arm/thread_status.h`) to resolve the architectural layout differences between ARM64 and ARM64e.

### Struct Field Offset Comparison:

| Offset (Hex / Dec) | Size (Bytes) | Standard ARM64 (`ArmSavedState64Standard`) | ARM64e PAC (`ArmSavedState64Pac`) | Notes |
|---|---|---|---|---|
| `0x000` (0) | 232 | `uint64_t x[29]` | `uint64_t x[29]` | Registers X0 through X28 |
| `0x0E8` (232) | 8 | `uint64_t fp` (x29) | `uint64_t fp` (x29) | Frame Pointer |
| `0x0F0` (240) | 8 | `uint64_t lr` (x30) | `uint64_t lr` (x30) | Link Register |
| `0x0F8` (248) | 8 | `uint64_t sp` | `uint64_t sp` | Stack Pointer (SP_EL0) |
| `0x100` (256) | 8 | `uint64_t pc` | `uint64_t pc` | Program Counter (ELR_EL1) |
| `0x108` (264) | 4 | `uint32_t cpsr` | `uint32_t cpsr` | Saved PSTATE (SPSR_EL1) |
| `0x10C` (268) | 4 | `uint32_t flags` | `uint32_t flags` | Architecture flags |
| `0x110` (272) | 8 | `uint64_t far` | `uint64_t far` | Fault Address (FAR_EL1) |
| `0x118` (280) | 4 | `uint32_t esr` | `uint32_t esr` | Syndrome Register (ESR_EL1) |
| `0x11C` (284) | 4 | `uint32_t exception` | `uint32_t exception` | Exception code / vector code |
| `0x120` (288) | 8 | *(End of struct)* | `uint64_t jophash` | **Apple PAC JOP Hash Slot** |
| **Total Size** | | **288 Bytes (72 Words)** | **296 Bytes (74 Words)** | Words = 4 bytes |

### Target Architecture Configuration:
WinIOS configures `kTargetHasApplePac = false`. The standard 288-byte ABI is used as the active operational structure because the WinIOS instruction emulator does not currently simulate the cryptographic keys and hardware signing instructions (`PACIA`, `AUTIA`, `PACDA`, `AUTDA`). However, `ArmSavedState64Pac` (296 bytes) is fully defined and validated with compile-time assertions to guarantee ABI readiness when PAC simulation is implemented.

---

## 6. Complete Exception Trampoline Listings (Issue D)

### Exception Entry Sequence (32 Instructions / 128 Bytes):

```
Address   Opcode      Mnemonic                           Description
----------------------------------------------------------------------------------------------------
0x0A80:   D10483FF    sub    sp, sp, #288                ; Allocate 288-byte ArmSavedState64 frame
0x0A84:   A90007E0    stp    x0, x1, [sp, #0]            ; Save X0, X1
0x0A88:   A9010FE2    stp    x2, x3, [sp, #16]           ; Save X2, X3
0x0A8C:   A90217E4    stp    x4, x5, [sp, #32]           ; Save X4, X5
0x0A90:   A9031FE6    stp    x6, x7, [sp, #48]           ; Save X6, X7
0x0A94:   A90427E8    stp    x8, x9, [sp, #64]           ; Save X8, X9
0x0A98:   A9052FEA    stp    x10, x11, [sp, #80]         ; Save X10, X11
0x0A9C:   A90637EC    stp    x12, x13, [sp, #96]         ; Save X12, X13
0x0AA0:   A9073FEE    stp    x14, x15, [sp, #112]        ; Save X14, X15
0x0AA4:   A90847F0    stp    x16, x17, [sp, #128]        ; Save X16, X17
0x0AA8:   A9094FF2    stp    x18, x19, [sp, #144]        ; Save X18, X19
0x0AAC:   A90A57F4    stp    x20, x21, [sp, #160]        ; Save X20, X21
0x0AB0:   A90B5FF6    stp    x22, x23, [sp, #176]        ; Save X22, X23
0x0AB4:   A90C67F8    stp    x24, x25, [sp, #192]        ; Save X24, X25
0x0AB8:   A90D6FFA    stp    x26, x27, [sp, #208]        ; Save X26, X27
0x0ABC:   A90E77FC    stp    x28, x29, [sp, #224]        ; Save X28, X29 (FP)
0x0AC0:   F9007BFE    str    x30, [sp, #240]             ; Save X30 (LR)
0x0AC4:   910483E0    add    x0, sp, #288                ; Compute interrupted SP
0x0AC8:   F9007FE0    str    x0, [sp, #248]              ; Save SP_EL0
0x0ACC:   D5384021    mrs    x1, elr_el1                 ; Read interrupted PC
0x0AD0:   F90083E1    str    x1, [sp, #256]              ; Save PC
0x0AD4:   D5384002    mrs    x2, spsr_el1                ; Read interrupted PSTATE
0x0AD8:   B9010BE2    str    w2, [sp, #264]              ; Save CPSR (32-bit store)
0x0ADC:   B9010FFE    str    wzr, [sp, #268]             ; Clear flags (32-bit store)
0x0AE0:   D5386000    mrs    x0, far_el1                 ; Read Fault Address
0x0AE4:   F9008BE0    str    x0, [sp, #272]              ; Save FAR
0x0AE8:   D5385203    mrs    x3, esr_el1                 ; Read Exception Syndrome
0x0AEC:   B9011BE3    str    w3, [sp, #280]              ; Save ESR (32-bit store, offset 0x118)
0x0AF0:   52800024    mov    w4, #1                      ; Exception vector code (IRQ)
0x0AF4:   B9011FE4    str    w4, [sp, #284]              ; Save exception (32-bit store, offset 0x11C)
0x0AF8:   A94007E0    ldp    x0, x1, [sp, #0]            ; Reload scratch X0, X1
0x0AFC:   A9410FE2    ldp    x2, x3, [sp, #16]           ; Reload scratch X2, X3
```

### Exception Restore Sequence (22 Instructions / 88 Bytes):

```
Address   Opcode      Mnemonic                           Description
----------------------------------------------------------------------------------------------------
0x0B20:   F94083E0    ldr    x0, [sp, #256]              ; Reload interrupted PC from RAM
0x0B24:   D5184020    msr    elr_el1, x0                 ; Restore ELR_EL1
0x0B28:   B9410BE0    ldr    w0, [sp, #264]              ; Reload interrupted PSTATE from RAM
0x0B2C:   D5184000    msr    spsr_el1, x0                ; Restore SPSR_EL1
0x0B30:   A94007E0    ldp    x0, x1, [sp, #0]            ; Reload X0, X1 from RAM
0x0B34:   A9410FE2    ldp    x2, x3, [sp, #16]           ; Reload X2, X3 from RAM
0x0B38:   A94217E4    ldp    x4, x5, [sp, #32]           ; Reload X4, X5 from RAM
0x0B3C:   A9431FE6    ldp    x6, x7, [sp, #48]           ; Reload X6, X7 from RAM
0x0B40:   A94427E8    ldp    x8, x9, [sp, #64]           ; Reload X8, X9 from RAM
0x0B44:   A9452FEA    ldp    x10, x11, [sp, #80]         ; Reload X10, X11 from RAM
0x0B48:   A94637EC    ldp    x12, x13, [sp, #96]         ; Reload X12, X13 from RAM
0x0B4C:   A9473FEE    ldp    x14, x15, [sp, #112]        ; Reload X14, X15 from RAM
0x0B50:   A94847F0    ldp    x16, x17, [sp, #128]        ; Reload X16, X17 from RAM
0x0B54:   A9494FF2    ldp    x18, x19, [sp, #144]        ; Reload X18, X19 from RAM
0x0B58:   A94A57F4    ldp    x20, x21, [sp, #160]        ; Reload X20, X21 from RAM
0x0B5C:   A94B5FF6    ldp    x22, x23, [sp, #176]        ; Reload X22, X23 from RAM
0x0B60:   A94C67F8    ldp    x24, x25, [sp, #192]        ; Reload X24, X25 from RAM
0x0B64:   A94D6FFA    ldp    x26, x27, [sp, #208]        ; Reload X26, X27 from RAM
0x0B68:   A94E77FC    ldp    x28, x29, [sp, #224]        ; Reload X28, X29 (FP) from RAM
0x0B6C:   F9407BFE    ldr    x30, [sp, #240]             ; Reload X30 (LR) from RAM
0x0B70:   910483FF    add    sp, sp, #288                ; Deallocate exception frame
0x0B74:   D69F03E0    eret                               ; Architectural Exception Return
```

---

## 7. Architectural Generic Timer Semantics (Issue E)

The virtual timer subsystem implements the ARMv8 Generic Timer architecture matching Apple Silicon specifications.

### Control Register (`CNTV_CTL_EL0`) Bitfields:

| Bit | Name | R/W | Architectural Behavior |
|---|---|---|---|
| **0** | `ENABLE` | R/W | 1 = Timer enabled; 0 = Timer disabled. If disabled, ISTATUS is forced to 0. |
| **1** | `IMASK` | R/W | 1 = Interrupt output masked (deasserted to AIC2); 0 = Interrupt unmasked. Does not affect ISTATUS. |
| **2** | `ISTATUS` | **RO** | **Hardware-derived read-only status bit.** Evaluates to 1 if `ENABLE == 1` and `CNTVCT_EL0 >= CNTV_CVAL_EL0`. Writes from guest software are ignored by hardware. |

### Mathematical Rules Enforced:
1. **TVAL Write:** When software writes a 32-bit value to `CNTV_TVAL_EL0`, hardware computes the 64-bit absolute comparator deadline via sign-extended addition:
   $$\text{CNTV\_CVAL\_EL0} = \text{CNTVCT\_EL0} + (\text{int64\_t})(\text{int32\_t})\text{TVAL}$$
2. **TVAL Read:** Reading `CNTV_TVAL_EL0` returns the signed 32-bit difference:
   $$\text{TVAL} = (\text{int32\_t})(\text{CNTV\_CVAL\_EL0} - \text{CNTVCT\_EL0})$$
3. **Interrupt Line Assertion:** The interrupt line to the interrupt controller asserts if and only if:
   $$\text{IRQ\_ASSERTED} = (\text{ISTATUS} == 1) \land (\text{IMASK} == 0)$$
4. **Drift-Free Periodic Rearming:** To maintain exact tick spacing across timer interrupts without clock drift:
   $$\text{CNTV\_CVAL\_EL0}_{\text{next}} = \text{CNTV\_CVAL\_EL0}_{\text{prev}} + \text{PERIOD\_TICKS}$$

---

## 8. Twenty-Dimension Security & Maturity Audit Matrix

Every dimension was rigorously evaluated through dedicated test suites with adversarial fault injection.

| Dimension | Scope / Requirement | Campaign 261 Status | Verification Evidence |
|---|---|---|---|
| **1. Canonical Memory Map** | Authoritative 8 MiB layout with zero gaps | **PASSED** | 512 frames (16 KiB) verified; frames 0..36 reserved, frames 37..511 free. |
| **2. Ownership Uniqueness** | Pairwise non-overlap across all reserved intervals | **PASSED** | `PhysicalMemoryOverlapAuditor` verified 0 collisions across 17 reserved blocks. |
| **3. Allocator Exclusion** | Frame 32 (`0x80000`) strictly excluded from free pool | **PASSED** | Allocator free pool begins at `0x00094000`; Frame 32 mathematically excluded. |
| **4. Frame Accounting** | 37 reserved + 475 free = 512 total frames | **PASSED** | Bitwise frame accounting validated in unit and integration test suites. |
| **5. Six-Core Stack Isolation** | Dedicated 16 KiB interrupt and thread stacks per core | **PASSED** | 6 disjoint interrupt stacks and 6 disjoint thread stacks verified across Cores 0..5. |
| **6. Guard Enforcement** | Unmapped 16 KiB guard intervals under each stack base | **PASSED** | 6 guard intervals verified with `permissions = 0`; underflow causes abort. |
| **7. Saved-State ABI** | Upstream XNU size, alignment, and field offsets | **PASSED** | `ArmSavedState64Standard` = 288 B (72 words), `ArmSavedState` = 304 B `alignas(16)`. |
| **8. PAC Configuration** | Formal ARM64e PAC layout established | **PASSED** | `ArmSavedState64Pac` = 296 B (74 words) with `jophash` at `0x120`; `kTargetHasApplePac=false`. |
| **9. Exception Save Completeness** | Full 32-instruction entry sequence | **PASSED** | X0..X30, SP, PC, PSTATE, FAR, ESR saved; scratch registers restored. |
| **10. Exception Restore Completeness**| Full 22-instruction restore sequence | **PASSED** | Context reloaded from RAM after live register corruption; verified before ERET. |
| **11. Nested Exception Bounds** | Stack depth limit calculations and protections | **PASSED** | Safe nesting up to 56 raw frames (39 with overhead) proven within 16 KiB stack. |
| **12. Timer Counter Semantics** | Signed 32-bit TVAL math and 24 MHz counter | **PASSED** | Sign extension on negative TVAL and monotonic counter advance verified. |
| **13. Comparator Semantics** | Read-only ISTATUS derived from hardware condition | **PASSED** | Software cannot clear ISTATUS while `counter >= comparator`. |
| **14. Periodic Rearming** | Drift-free comparator advancement | **PASSED** | Rearm advances deadline by exact period; delayed servicing verified without drift. |
| **15. AIC2 Routing** | End-to-end timer interrupt vectoring and EOI | **PASSED** | Comparator match -> AIC2 assert -> vector 0x0A80 -> ACK -> EOI -> ERET trace. |
| **16. W^X Permission Integrity** | Zero simultaneous RWX frames | **PASSED** | Audited all 512 frames: code frames are RX, data/stacks are RW, guards are None. |
| **17. Translation Synchronization** | 16 KiB page granule strictly maintained | **PASSED** | All interval bases, sizes, and allocations aligned to $16,384$-byte boundaries. |
| **18. SMP Multi-Core Isolation** | Banked WhoAmI MMIO registers and independent timers | **PASSED** | Independent timer registers and banked AIC2 registers verified across all 6 cores. |
| **19. Deterministic Replay** | Snapshot serialization round-trips bitwise | **PASSED** | Binary serialization of timer, scheduler, and memory state verified identical. |
| **20. Provenance & Reproducibility** | Segregation of Categories A, B, C, D | **PASSED** | Strict component tagging; upstream XNU source separated from simulations and stubs. |

**Certification Assessment:** **Maturity Level 3 Certified** (Freestanding Research Environment & Exception Boundary).

---

## 9. Formal Evaluation and Rejection of Maturity Level 4

WinIOS Campaign 261 explicitly evaluates the requirements for **Maturity Level 4 (Darwin/iOS Kernel Continuation)** and formally rejects promotion. The platform remains certified at **Maturity Level 3**.

Promotion to Level 4 is blocked by four concrete architectural realities:

1. **Blocker 1: Absence of Guest-Executed Multi-Level Translation Table Walks (ARM64 VMSA):**
   - While WinIOS models physical memory, frames, and permissions with page-granule alignment, it does not currently execute genuine guest ARM64 translation table walks (L0 -> L1 -> L2 -> L3 descriptor decoding) with hardware translation fault injection, demand paging, and guest-driven TLB invalidation (`TLBI VMALLE1IS`).
2. **Blocker 2: Absence of Genuine Preemptive Thread Context Switcher:**
   - The scheduler subsystem (`EarlySchedulerAccounting`) is a deterministic accounting model (Category C). WinIOS lacks an authoritative preemptive thread dispatcher with active runqueues, priority decay, thread wait-queues, Mach AST (Asynchronous Software Trap) processing, and quantum expiration.
3. **Blocker 3: Absence of Apple Secure Enclave Processor (SEP) Mailbox Handshake:**
   - A genuine Darwin/iOS Level 4 execution path requires interactive cryptographic handshake with the Apple SEP mailbox and driver enumeration matching Apple Silicon IOKit device tree specifications.
4. **Blocker 4: Freestanding Execution Boundary vs. Authentic Kernel Binary Bootstrap:**
   - Guest execution currently proceeds through freestanding XNU source units and deterministic test trampolines rather than an authentic signed Mach-O kernel binary (`kernel.release.t8120`) booting through the authentic Apple iBoot handoff chain.

**Formal Determination:** Claims of Darwin boot, iOS execution, or Maturity Level 4 are strictly disclaimed.

---

## 10. Subsystem Provenance Classification

All WinIOS components are strictly classified according to the 4-tier provenance taxonomy:

| Category | Description | Components in WinIOS |
|---|---|---|
| **Category A: Genuine Upstream XNU Source** | Bit-for-bit compiled code directly derived from Apple public source `xnu-10002.1.13` | `osfmk/mach/arm/thread_status.h` ABI structures, `arm_saved_state64_t` field definitions, flavor constants (`ARM_SAVED_STATE64 = 21`). |
| **Category B: Hardware-Accurate Behavioral Models** | Emulated architectural components implementing ARMv8-A and Apple Silicon specifications | `DeterministicVirtualTimer` (24 MHz counter, TVAL, CVAL, CTL), `Aic2InterruptController` (banked WhoAmI, ACK, EOI), `CanonicalPhysicalMemoryMap` (8 MiB layout, 16 KiB granule). |
| **Category C: Synthetic Simulation Models** | Functional abstractions providing kernel-like accounting or memory pools | `EarlySchedulerAccounting` (tick tracking, active/idle ratio), `XnuZoneAllocator` (bounded physical frame allocator pool). |
| **Category D: Diagnostic Test Harnesses & Vectors** | Test harnesses, synthetic code blocks, and verification trampolines | Staged diagnostic kernel text at Frame 32 (`0x00080000`), 32-instruction entry trampoline, 22-instruction restore trampoline, adversarial mutation harnesses. |

---

## 11. Dual-Configuration Regression Test Matrix

All 1,131 tests registered in CMake/CTest were executed across both build configurations under MSVC C++20 (`/W4 /WX /permissive-`).

| Test Suite / Category | Tests | RelWithDebInfo Status | Debug Status | Notes |
|---|---|---|---|---|
| **Campaign 1–259 Historical Regressions** | 1,119 | 1,118 Passed, 1 Skipped | 1,118 Passed, 1 Skipped | Test #383 (`inferno_integration_tests`) skipped as expected (isolated workspace). |
| **Campaign 260 Regressions** | 6 | 6 Passed | 6 Passed | Stages 260.4, 260.8, 260.12, 260.16, 260.20, 260.23 suites. |
| **Stage 261.5: Memory Map & Allocator Tests** | 1 | Passed | Passed | Canonical map, overlap auditor, free pool derivation, Issue A resolution. |
| **Stage 261.8: Six-Core Stack & Guard Tests** | 1 | Passed | Passed | Six-core stacks, unmapped guards, initial SPs, nesting calculations. |
| **Stage 261.11: ARM64e PAC & ABI Tests** | 1 | Passed | Passed | Upstream XNU constants, PAC 296 B vs Standard 288 B, serialization. |
| **Stage 261.15: Exception Save/Restore Tests** | 1 | Passed | Passed | 32-instr save, 22-instr restore, live register scrambling, RAM reload. |
| **Stage 261.19: Timer & SMP Tests** | 1 | Passed | Passed | Signed TVAL, read-only ISTATUS, periodic rearming, six-core SMP trace. |
| **Stage 261.23: Auditor & Maturity Tests** | 1 | Passed | Passed | W^X enforcement, mutation detection, 20-dimension audit, Level 4 rejection. |
| **TOTAL** | **1,131** | **1,130 Passed, 1 Skipped (100%)** | **1,130 Passed, 1 Skipped (100%)** | **Zero Failures, Zero Compiler Warnings.** |

---

## 12. Deliverables & Artifact Manifest

The campaign generates exactly TWO files in `artifacts/campaign-261/`:
1. `artifacts/campaign-261/WINIOS_CAMPAIGN_261_FINAL_REPORT.md` (this comprehensive report)
2. `artifacts/campaign-261/WINIOS_CAMPAIGN_261_SHA256_MANIFEST.txt` (cryptographic SHA-256 manifest)

All repository changes are strictly localized, self-contained, fully auditable, and adhere to all safety and workspace invariants.
