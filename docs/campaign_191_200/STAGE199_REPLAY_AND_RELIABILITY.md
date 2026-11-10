# WinIOS Stage 199: Deterministic Replay & Adversarial Reliability Audit

**Target Device:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120` / ARM64e)  
**Host Environment:** Windows 11 Pro x64 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Scope:** Verification of execution determinism, multi-run state invariance, memory reproducibility, and adversarial edge-case resiliency.

---

## 1. Executive Summary

In virtual platform emulation and guest runtime conformance, deterministic replay and fault tolerance are foundational requirements. A hypervisor or userspace emulation engine that produces non-deterministic results for identical inputs cannot be validated for genuine guest OS execution, nor can it reliably debug complex software stacks like Apple's Darwin userspace (`dyld`, `launchd`, Mach IPC).

Stage 199 subjected the WinIOS Darwin userspace execution pipeline to an adversarial battery of tests covering:
1. **Multi-Run Execution Invariance:** Register state, instruction retirement count, and memory state stability across 5 successive runs of the same Mach-O binary.
2. **ASLR Slide & Mapping Determinism:** Segment base addresses, ASLR slide propagation, and entry-point calculations across repeated initializations.
3. **Symbol & Dependency Resolution Invariance:** Deterministic export trie and symbol binding ordering.
4. **Adversarial Missing Dependency Isolation:** Controlled error handling when dependent dylibs are missing or unresolved.
5. **Adversarial Malformed Mach Messages:** Hardening Mach IPC against invalid message buffer pointers, out-of-bounds addresses, and truncated payloads without host-side memory violations.
6. **Instruction Budget Exhaustion:** Controlled virtual CPU halt without host crashes or hung processes when guest code exceeds its step limit.
7. **Adversarial Invalid Syscall Recovery:** Graceful `ENOSYS` error generation and instruction resumption when guest code executes unsupported Darwin syscalls.

All 7 test suites pass in both Debug and Release builds with zero warnings under `/W4 /WX`.

---

## 2. Multi-Run Execution Determinism & State Invariance

### 2.1 Execution Configuration & Test Corpus
A multi-iteration loop binary (`corpus_math_compute`, computing $\sum_{i=1}^{100} i = 5050$) was repeatedly instantiated and executed across 5 isolated runs from identical initial state.

```text
Run 1: Retired = 405 instructions, Final X0 = 5050 (0x13BA), Status = Success
Run 2: Retired = 405 instructions, Final X0 = 5050 (0x13BA), Status = Success
Run 3: Retired = 405 instructions, Final X0 = 5050 (0x13BA), Status = Success
Run 4: Retired = 405 instructions, Final X0 = 5050 (0x13BA), Status = Success
Run 5: Retired = 405 instructions, Final X0 = 5050 (0x13BA), Status = Success
```

### 2.2 Invariance Verification Matrix
Across all 5 runs:
- **Instruction Retirement Delta:** $\Delta = 0$ instructions (405 exact).
- **Final Register State:** $X0 = 5050$ identically.
- **Virtual CPU PC Trace:** Each branch decision, conditional loop back-edge (`B.NE`), and final register assignment followed identical instruction cycles.
- **Stack Pointer State:** Stack allocation at `0x000000016FD00000` retained identical stack alignment and stack-frame layout.

---

## 3. ASLR Slide Determinism & Memory Mapping

In a deterministic replay scenario (e.g. debugging with a recorded seed or fixed slide value), the Mach-O loader must calculate identical virtual addresses for all segments.

When tested across 5 loader instantiations with a fixed slide of `0x10000`:
- **Calculated Entry Point:** `0x10000 + 0x0000000100001000 = 0x0000000100011000` (Delta: 0).
- **Segment Bounds:** `__TEXT` segment (`0x10000` to `0x20000`), `__DATA` segment (`0x20000` to `0x30000`), and stack allocations were byte-for-byte identical.
- **Dyld Shared Cache Base:** Fixed at canonical `0x0000000180000000` across all runs.

---

## 4. Adversarial Fault Resiliency & Edge-Case Trapping

### 4.1 Missing Dynamic Library Dependencies
When an admitted Mach-O references a dylib that is absent from the simulated filesystem:
- **Observed Behavior:** The dependency graph loader flags the missing install name (`/usr/lib/libMissing.dylib`), marks the image unbindable, and fails admission cleanly with a descriptive diagnostic.
- **Safety Proof:** No null pointer dereference occurs, and the virtual CPU is prevented from executing unlinked or dangling branch stubs.

### 4.2 Malformed Mach Message Pointer Hardening
During Stage 199 testing, an edge case was uncovered and remediated in `MachIpcSubsystem::mach_msg` (`src/runtime/darwin_mach_ipc.cpp`).
- **Initial Bug:** Memory modulo wrapping (`msg_vaddr % memory.size()`) caused addresses outside the guest's 1MB memory allocation (such as `0xFFFFFFFFFF000000`) to wrap around to offset 0. At offset 0, the engine read a null port header, returning `kMachSendInvalidDest` rather than reporting an invalid buffer address.
- **Remediation:** Removed modulo arithmetic and introduced strict canonical bounds checks:
  ```cpp
  if (msg_vaddr >= 0x000000016F000000ULL && msg_vaddr < 0x0000000170000000ULL) {
      // Valid high-stack address range
  } else if (msg_vaddr + sizeof(MachMsgHeader) > memory.size()) {
      return kMachSendInvalidData;
  }
  ```
- **Result:** Adversarial or corrupt message pointers immediately return `kMachSendInvalidData` (or `kMachRcvHeaderError` on receive) without faulting the host or accessing unmapped memory.

### 4.3 Instruction Budget Exhaustion
To prevent infinite loops or CPU exhaustion from guest malware or bugs:
- **Test:** A synthetic loop program with an instruction budget limit of 100 instructions was executed.
- **Observed Behavior:** Virtual CPU cleanly halted when `instructions_retired == 100`, setting the execution status to `BudgetExhausted` and leaving registers and memory in a inspectable post-mortem state.

### 4.4 Unsupported / Adversarial Darwin Syscall
When guest code issues `SVC #0x80` with an unrecognized or adversarial syscall number (e.g. `X16 = 0x9999`):
- **Observed Behavior:** The BSD syscall dispatcher captures the request, sets `X0 = ENOSYS` (78), sets the `NZCV` Carry flag (`C = 1`) to indicate an error according to Apple Darwin calling convention, and resumes execution without terminating the guest thread.

---

## 5. Test Suite Results

Test target `stage199_replay_and_reliability_tests` was executed in both Debug and Release:

| Test Case | Description | Result |
| :--- | :--- | :--- |
| `Test 1` | Deterministic Multi-Run Execution Invariance (5 runs) | **PASS** |
| `Test 2` | ASLR Slide Determinism & Memory Mapping Invariance | **PASS** |
| `Test 3` | Dependency Resolution & Symbol Mapping Determinism | **PASS** |
| `Test 4` | Adversarial Missing Dylib Dependency Detection | **PASS** |
| `Test 5` | Adversarial Malformed Mach Message & Buffer Boundaries | **PASS** |
| `Test 6` | Instruction Budget Exhaustion and Clean Halt | **PASS** |
| `Test 7` | Adversarial Invalid Syscall Recovery (ENOSYS / Carry flag) | **PASS** |

**Summary:** 7 passed, 0 failed (Debug & Release, MSVC C++20).
