# Eight-Core MMIO and Entropy Input Replay

## 1. Non-Deterministic Peripheral Emulation
Reads from memory-mapped I/O (MMIO) and entropy/RNG sources are non-deterministic guest inputs. WinIOS captures the exact values returned during recording:
- `MmioReadResult`: Records core ID, physical address, access width (1, 2, 4, 8 bytes), and 64-bit value returned by the device.
- `EntropyReadResult`: Records core ID and random seed / entropy bits provided to the guest.

## 2. Replay Injection
During replay, the memory sub-system bypasses live peripheral devices when servicing MMIO reads, intercepting the memory transaction and returning the recorded payload from the `MmioReadResult` event.

## 3. Multicore Interleaving Protection
Each MMIO read event is tagged with the requesting `core_id` and global sequence number. In Unit Test Group 11, concurrent MMIO reads from cores 0, 3, 5, and 7 returned bit-identical values during replay in the exact order recorded.
