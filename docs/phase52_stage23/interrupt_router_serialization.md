# Cross-Core Interrupt Router State Serialization

## 1. Subsystem Architecture

The WinIOS cross-core interrupt router (`SmpInterruptRouter`) provides inter-core interrupt signaling and distributor routing with strict request-versus-delivery separation:
- **SGIs (INTID 0–15)**: Software Generated Interrupts triggered across cores.
- **PPIs (INTID 16–31)**: Private Peripheral Interrupts routed to core-local queues.
- **SPIs (INTID 32–1019)**: Shared Peripheral Interrupts routed to designated target cores.
- **Deterministic FIFO Queues**: Interrupt requests are enqueued monotonically and delivered strictly at instruction and quantum boundaries.

---

## 2. Binary Layout (`INTERRUPT_ROUTER` - 0x16)

### Header (16 Bytes)
- `max_cores` (u32): Core queue capacity.
- `reserved` (u32): Zero padding.
- `next_sequence` (u64): Monotonic sequence counter for assigning request sequence numbers.

### Core Queue Entries
Repeated for each core index $c \in [0, \text{max\_cores} - 1]$:
- `core_id` (u32): Target core queue index.
- `queue_size` (u32): Number of pending interrupt requests in this core's queue.
- Followed by `queue_size` serialized `InterruptRequest` records (24 bytes each):
  - `target_core` (u32): Intended recipient virtual core.
  - `source_core` (u32): Originating virtual core (for SGIs).
  - `intid` (u32): Interrupt identifier (0–1019).
  - `priority` (u8): Interrupt priority byte.
  - `type` (u8): InterruptType classification (0=SGI, 1=PPI, 2=SPI, 3=Extended).
  - `is_fiq` (u8): FIQ flag (1=FIQ, 0=IRQ).
  - `reserved` (u8): Alignment padding.
  - `request_sequence` (u64): Monotonic arrival order sequence.

---

## 3. Restoration & Ordering Guarantees

On restoration:
1. `max_cores` and queue count are verified against cluster configuration.
2. `next_sequence_` counter is restored to ensure continuous monotonicity for subsequent interrupts.
3. Each pending queue is cleared and populated in exact FIFO sequence.
4. Subsequent scheduling slices pop and deliver interrupts in the identical order as recorded before the snapshot was taken.

---

## 4. Test Verification

Test Group F (`F1_InterruptRouterRoundTrip`) verifies cross-core SGIs and SPIs pending across cores, asserting that INTID, priority, sequence number, and FIFO order are preserved identically.
