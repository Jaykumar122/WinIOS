# Exclusive-Monitor State Serialization

## 1. Subsystem Architecture

The WinIOS cluster exclusive monitor (`SmpExclusiveMonitorManager`) implements atomic memory synchronization across virtual cores following ARMv8-A architecture guidelines (DDI 0487):
- **Reservation Granule**: 64 bytes (`kGranuleSize = 64`).
- **Load-Exclusive (`LDXR`)**: Establishes a reservation for the calling core at the aligned 64-byte granule.
- **Store-Exclusive (`STXR`)**: Evaluates reservation validity; if valid, executes the write and clears the reservation; if invalid, returns failure without mutating memory.
- **Cross-Core Invalidation**: Any ordinary or exclusive store to an address clears all overlapping reservations held by other virtual cores.
- **Clear-Exclusive (`CLREX`)**: Explicitly clears calling core reservation.

---

## 2. Binary Layout (`EXCLUSIVE_MONITOR` - 0x15)

### Header (8 Bytes)
- `max_cores` (u32): Maximum core reservation capacity.
- `reservation_count` (u32): Count of serialized reservation entries.

### Reservation Entries ($32 \times \text{reservation\_count}$ Bytes)
For each reservation entry:
- `core_id` (u32): Core owning the reservation.
- `valid` (u32): 1 if reservation is actively held, 0 if cleared.
- `granule_address` (u64): Aligned 64-byte granule base address (`address & ~0x3FULL`).
- `exact_address` (u64): Architectural byte address targeted by `LDXR`.
- `size` (u64): Access width (1, 2, 4, 8, or 16 bytes).

---

## 3. Restoration Integrity & Semantic Validation

On deserialization:
1. `core_id` must be $< \text{max\_cores}$.
2. `granule_address` must be 64-byte aligned (`granule_address & 0x3F == 0`).
3. `exact_address & ~0x3F` must equal `granule_address`.
4. `size` must be a valid architectural access width ($1, 2, 4, 8, 16$).
5. Target `SmpExclusiveMonitorManager` resets all entries, then installs the deserialized reservation states.

---

## 4. Test Verification

Test Group E (`E1_ExclusiveMonitorRoundTrip`) validates that active and cleared reservations across multiple cores round-trip with exact fidelity, and subsequent `STXR` operations correctly evaluate reservation validity against restored granules.
