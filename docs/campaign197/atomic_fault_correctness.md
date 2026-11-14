# WinIOS Campaign 197 Stage 197.8: Atomic Operation Fault Correctness

**Document ID:** `docs/campaign197/atomic_fault_correctness.md`  
**Classification:** `GUEST_ATOMIC`  
**Acceptance Test:** Acceptance Test F (Atomic accesses do not partially modify memory on faults)

---

## 1. Architectural Imperative

Under ARMv8-A / ARMv8.1-A (ARM DDI 0487 Section B2.9 and C6.2), all atomic and exclusive instructions (`LDXR`, `STXR`, `CAS`, `SWP`, `LDADD`, `LDCLR`, `LDEOR`, `LDSET`, etc.) are single architectural operations.

If an atomic memory operation encounters any synchronous fault during execution:
1. **Alignment Fault:** Attempting an atomic access at an unaligned address (e.g. 64-bit access at address not divisible by 8) must trigger an Alignment Fault with `WnR = 1`.
2. **Translation Fault:** Accessing an unmapped virtual address triggers a Translation Fault.
3. **Permission Fault:** Attempting an atomic write on a read-only page triggers a Permission Fault.

**Non-Partial Modification Invariant:**  
When an atomic instruction faults, **zero** bytes of memory may be updated. The operation must fail cleanly and completely, preserving the pre-fault memory contents across the entire width of the requested access. Registers must remain untouched, and the syndrome must be delivered cleanly to the exception vector.

---

## 2. Implementation Architecture

In WinIOS `src/cpu/cpu.cpp`:
- `CPU::step()` performs upfront access validation via `memory.check_access(address, width, AccessType::Write, architectural_exceptions_)` **before** any memory modification or register writeback occurs.
- If `check_access` detects an unaligned address, an unmapped address, or an invalid permission:
  - It constructs a `SynchronousException` with accurate syndrome (`ESR_EL1`) and fault address (`FAR_EL1`).
  - It raises a synchronous exception immediately.
  - The instruction step terminates without committing any memory writes or register changes.

---

## 3. Verification Matrix

The test suite `tests/unit/stage197_8_atomic_fault_tests.cpp` explicitly validates the non-partial modification invariant:

| Test Case | Instruction | Access Width | Fault Type | Pre-Fault Memory | Post-Fault Memory | Verdict |
|---|---|---|---|---|---|---|
| Unaligned LDADD | `LDADD` | 64-bit | Alignment Fault | `0x0123456789ABCDEF` | `0x0123456789ABCDEF` (0 bytes modified) | **PASSED** |
| Unaligned CAS | `CAS` | 64-bit | Alignment Fault | `0x0123456789ABCDEF` | `0x0123456789ABCDEF` (0 bytes modified) | **PASSED** |
| Unaligned SWP | `SWP` | 64-bit | Alignment Fault | `0x0123456789ABCDEF` | `0x0123456789ABCDEF` (0 bytes modified) | **PASSED** |
| Unmapped CAS | `CAS` | 64-bit | Translation Fault | Unmapped | Unmodified / Clean Exception Taken | **PASSED** |

Execution confirmed via `stage197_8_atomic_fault_tests.exe`: **100% Pass**.
