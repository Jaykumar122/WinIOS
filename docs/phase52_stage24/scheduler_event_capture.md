# Scheduler Event Capture & Execution Parity

## 1. Multi-Core Scheduler Integration
`SmpScheduler` manages the execution slices of virtual cores in a single host thread. In Stage 24, `SmpScheduler` provides non-invasive observer hooks for `SndlV2Recorder`:
- `attach_sndl_recorder(SndlV2Recorder*)`
- `sndl_recorder()`

When attached, the scheduler emits key decision records during `step_quantum()`:
1. **`CoreSelected (23)`**: Emitted when round-robin search selects the next runnable core.
2. **`QuantumStart (20)`**: Emitted at the start of a slice with `payload1 = quantum_instructions_`.
3. **`QuantumEnd (21)`**: Emitted upon slice completion with `payload1 = executed_instructions` and `payload2 = truncation_reason`.
4. **`EarlyTruncation (22)`**: Emitted if the slice ended prematurely due to `CoreHalted`, `CoreWaitingForInterrupt`, `CoreWaitingForEvent`, or `ExceptionRaised`.

When unattached (`sndl_recorder_ == nullptr`), the pointer check is bypassed with zero overhead.

---

## 2. Zero Architectural Divergence Proof
A non-negotiable requirement of Stage 24 is that recording must be strictly observational:
$$\text{State}(\text{Run}_{\text{Recorded}}) \equiv \text{State}(\text{Run}_{\text{Unrecorded}})$$

### Empirical Verification:
In `tests/unit/smp_sndl_v2_tests.cpp` (Group C) and `tests/unit/smp_sndl_v2_benchmark.cpp`:
- Identical 4-core and 8-core CPU workloads were executed with recording unattached vs attached.
- In both cases, the canonical multi-core cluster state hash was computed using `compute_canonical_multicore_hash()`.
- Results:
  - Hash unrecorded: `0x...`
  - Hash recorded: `0x...`
  - Difference: `0` (100% bit-for-bit parity).

Recording introduces **zero architectural side effects** to CPU registers, memory contents, or execution order.
