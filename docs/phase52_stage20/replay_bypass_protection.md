# Replay Bypass Protection & Host Leakage Prevention

## 1. Threat Model: Replay Bypass & Host Leakage

In a record/replay virtualization system, "replay bypass" refers to any execution path in the guest interpreter that queries or depends upon the host execution environment rather than the recorded log. Even a single leaked bit from the host environment breaks determinism, leading to catastrophic divergence:

1. **Host Wall-Clock Leakage**: A guest timer or counter handler querying Windows APIs (`QueryPerformanceCounter`, `GetSystemTimePreciseAsFileTime`, `std::chrono::high_resolution_clock`) will observe differing timing across separate replay runs or different host machines.
2. **Peripheral & Device Leakage**: Querying a live host peripheral (e.g. host disk, console, network socket) during replay instead of returning recorded MMIO data causes unpredictable values.
3. **Host Address / Pointer Leakage**: Storing raw 64-bit host C++ pointers in guest architectural structures or incorporating host pointer addresses into architectural state hashes causes instant divergence across runs due to host ASLR.
4. **Uninitialized Guest State**: Failing to clear or restore registers, flags, or memory buffers allows residual host process memory to influence execution.

---

## 2. Hardened Architectural Defenses

WinIOS enforces five strict layers of protection against replay bypass:

### Layer 1: Strict VirtualTime Isolation
During replay, all time advancement is completely decoupled from the host CPU. Time progresses exclusively as a deterministic function of guest instruction counts (`instructions_executed * 1,000,000,000 / frequency`) or explicit timer step calls. The host OS clock is never queried by the CPU, PMU, or timer logic.

### Layer 2: MMIO Interception & Fallback Blocking
When `strict_bypass_checks = true` is configured in `ReplayConfig`, any MMIO read targeting an address for which there is no matching recorded event in the SNDL stream is treated as an illegal unlogged access. The router refuses to query live peripheral emulation, immediately triggering a divergence tripwire (`DivergenceCategory::MmioReadMismatch`).

### Layer 3: System Register & Entropy Virtualization
System registers that represent physical counters (`CNTPCT_EL0`, `CNTVCT_EL0`, `PMCCNTR_EL0`) and entropy sources (`RNDR`, `RNDRRS`) are bound to the `ReplayInputRouter`. When an instruction reads these registers, the value is supplied from the SNDL event log. If the guest executes an entropy query without a recorded log entry, the replay engine detects missing input and terminates.

### Layer 4: Host Pointer Immunity in State Hashing
The `ReplayStateHash` engine is strictly canonicalized. It hashes only architectural registers (`X0`–`X30`, `PC`, `SP`, `PSTATE`, `V0`–`V31`, system registers) and physical RAM page contents. Raw host `this` pointers, object addresses, page table host wrappers, and unaligned struct padding bytes are explicitly excluded.

### Layer 5: Clean Session Lifecycle & Router Teardown
`ReplaySession::close()` explicitly detaches the input router, unbinds VirtualTime and GIC hooks, and resets CPU state pointers. This prevents subsequent runs in the same host process from inheriting stale hooks or leaking router state across sessions.

---

## 3. Adversarial Leakage Verification (Test Group B)

Test Group B (`test_group_b_bypass_prevention` in `tests/unit/replay_hardening_tests.cpp`) verifies that bypass attempts are caught immediately:
- A malicious test scenario steps the CPU under `VirtualTime` without recording an external time jump.
- An unlogged peripheral read is attempted.
- The engine detects the unrecorded input condition, raises a divergence event with `DivergenceCategory::MmioReadMismatch`, halts execution safely, and records `ReplayCompletionStatus::Diverged`.
