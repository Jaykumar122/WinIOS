# Four-Core Interrupt Replay

## 1. Interrupt Router Subsystem

`SmpInterruptRouter` routes GICv3 interrupt signals across cores:
- **SPI (Shared Peripheral Interrupts)**: INTID 32..1019. Can be routed to any core.
- **PPI (Private Peripheral Interrupts)**: INTID 16..31. Core-private.
- **SGI (Software Generated Interrupts)**: INTID 0..15. Inter-core signaling.

---

## 2. Four-Core Interrupt Injection Protocol

In `test_group_9_interrupt_replay`:
1. External interrupts targeting different cores are scheduled:
   - Core 0 receives SPI 32.
   - Core 1 receives SPI 33.
   - Core 2 receives SPI 34.
   - Core 3 receives SPI 35.
2. During replay, `process_pending_external_events()` consumes each `ExternalInterruptAsserted` event and delivers it via:
   ```cpp
   scheduler_->interrupt_router().request_interrupt(
       consumed.core_id,
       static_cast<uint32_t>(consumed.payload1),
       static_cast<uint8_t>(consumed.payload2),
       consumed.source_id
   );
   ```
3. Target virtual CPUs acknowledge and take exception interrupts deterministically at exact instruction boundaries.
