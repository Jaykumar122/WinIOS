# WinIOS Campaign 197: Stage 197.15 Report
## Preemption and Interrupt Boundaries on Memory Operations (Acceptance Test L)

**Target Device:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Architecture:** Windows 11 x64, Visual Studio 18 2026, MSVC C++20  
**Evidence Classification:** `GUEST_ATOMIC` / `GUEST_EL1` / `SYNTHETIC_KERNEL`  
**Test Suite:** `tests/unit/stage197_15_interrupt_atomic_tests.cpp`  

---

### 1. Architectural Invariant

In ARMv8-A architecture (ARM DDI 0487 Section B2.9.2 "Load-Exclusive and Store-Exclusive instructions" and Section D1.10 "Exception handling"):
1. Taking an exception or an asynchronous interrupt (IRQ/FIQ) must clear the local exclusive monitor.
2. Returning from an exception via `ERET` must clear the local exclusive monitor.
3. If an interrupt preempts an in-flight LL/SC sequence between `LDXR` and `STXR`:
   - The monitor is invalidated upon interrupt delivery.
   - The interrupt handler preserves caller state and runs to completion.
   - Upon `ERET`, the interrupted context resumes with the monitor in the invalid state.
   - The pending `STXR` executes, detects `valid == false`, aborts the store without modifying memory, and writes `1` (failure status) to the status register $Ws$.
   - The guest retry loop branches back to `LDXR` to re-establish the reservation and retry the transaction cleanly.
4. For single-instruction LSE atomics (`CAS`, `SWP`, `LDADD`, etc.):
   - The instruction is completely atomic; an interrupt is delivered either strictly before instruction execution begins (instruction is restarted post-ERET) or strictly after it completes. No torn memory state or register corruption is possible.

---

### 2. Empirical Test Validation

```
  Normal Execution                     Interrupted Execution
  ----------------                     ---------------------
  0x1000: LDXR X1, [X0]                0x1000: LDXR X1, [X0]  (Monitor = VALID)
          (Monitor = VALID)                     --- ASYNCHRONOUS IRQ FIRED ---
  0x1004: STXR W2, X3, [X0]                     deliver_architectural_exception()
          (Success: W2=0, Mem updated)          (Monitor = INVALID, PC = VBAR+0x280)
                                                Interrupt Handler runs...
                                                ERET (Monitor = INVALID, PC = 0x1004)
                                       0x1004: STXR W2, X3, [X0]
                                                (FAILED! W2=1, Memory UNTOUCHED)
                                       0x1008: CBNZ W2, retry -> re-execute LDXR!
```

| Test Case | Scenario Tested | Architectural Expectation | Verified Outcome | Verdict |
|:---|:---|:---|:---|:---:|
| `test_interrupt_between_ldxr_and_stxr` | IRQ fired after LDXR, before STXR | Monitor cleared; STXR returns $W_2=1$; memory untouched; retry succeeds | $W_2=1$, memory unchanged, retry $W_2=0$ | **PASS** |
| `test_interrupt_preserves_lse_atomic_correctness` | IRQ delivered immediately before `LDADD` | Exception taken; ELR saves PC; post-ERET executes atomically | Clean resume; memory = 125, $X_2=100$ | **PASS** |

---

### 3. Conformance Statement

WinIOS enforces all ARMv8-A Section B2.9 and D1.10 requirements regarding exception and interrupt handling during exclusive and atomic operations. Interrupt preemption never corrupts guest memory or causes false-positive store-exclusive commits.
