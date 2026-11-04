# External Host Input Capture & Replay

## 1. Scope of External Inputs

In a deterministic execution environment, any data entering guest architectural state from outside the deterministic CPU/memory model constitutes an external input.

In WinIOS Stage 27, supported external inputs comprise:
1. **MMIO Read Data**: Programmed I/O responses from virtual devices (`MmioReadResult`).
2. **Entropy Generation**: Cryptographic hardware RNG output (`EntropyReadResult`).
3. **Interrupt Assertions**: External device line signals delivered to virtual GICv3 (`ExternalInterruptAsserted`).
4. **Timer Expirations**: Hardware comparator triggers (`TimerComparatorExpired`).

---

## 2. Event Representation and Delivery

Every external input event recorded in SNDL v2 includes:
- `global_event_sequence`: Monotonic index in global stream.
- `core_id`: Target virtual core (0..3).
- `core_retirement_count`: Core retirement timestamp at which the event is delivered.
- `payload1` / `payload2`: Input data bytes (e.g. MMIO read value, entropy bits, interrupt ID).

### Causal Ordering & Delivery
1. Events are checked before core instruction execution.
2. In `SmpReplayCoordinator::process_pending_external_events()`:
   - Synchronous external interrupts are queued into `SmpInterruptRouter`.
   - MMIO and entropy records are consumed in sequence with verified payload data.
3. Live host I/O is bypassed completely during replay, guaranteeing deterministic input feeding.
