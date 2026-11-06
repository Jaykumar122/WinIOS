# Interrupt Storm Replay Stress Results

## 1. Stress Scenario Design
An interrupt storm was simulated across an 8-core cluster consisting of 50 asynchronous interrupt events injected in rapid bursts:
- 25 Software Generated Interrupts (SGIs, INTID 0–15) routed across random core pairs ($Core_i \to Core_j$).
- 25 Peripheral / External SPI interrupts (INTID 32–63) asserted and cleared via `SmpInterruptRouter`.

## 2. Event Ordering and Capture
All 50 interrupt transitions were serialized into the SNDL v2 stream with monotonic global event sequence numbers ($E_{seq} \in [1, 50]$) and bound to precise per-core retirement counters ($R_{core}$).

## 3. Replay Verification
During replay:
- The coordinator consumed all 50 events in exact sequence order.
- Each SGI and SPI was delivered to the designated core's interrupt queue at the identical retirement boundary.
- Zero race conditions, interrupt drops, or queue deadlocks occurred.
- Final cluster hash matched the recorded baseline bit-for-bit.
