# Eight-Core External Input Replay Architecture

## 1. Unified External Input Replay Pipeline
External inputs comprise all events originating outside the deterministic CPU execution pipeline:
1. Physical device interrupts.
2. MMIO status and data registers.
3. Hardware entropy sources.
4. Asynchronous host events (e.g., user input injection).

## 2. Event Tagging & Sequencing
All external inputs are tagged with:
- Global Event Sequence ($S$)
- Global Retirement Sequence ($R$)
- Core Retirement Sequence ($r_c$)
- Target Core ID ($c \in [0..7]$)

## 3. Barrier Alignment
Before an external input event is delivered, the coordinator verifies that the target core has reached the exact retirement count specified in the event record. Replay guarantees that external inputs take effect at the exact micro-architectural boundary where they were sampled during recording.
