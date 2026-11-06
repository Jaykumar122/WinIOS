# WinIOS — Device Virtualization Subsystem

## 1. Overview
The Device Virtualization subsystem provides deterministic memory-mapped I/O (MMIO) peripheral modeling in WinIOS. It replaces ad-hoc register intercept hooks with an extensible, bus-driven device architecture.

## 2. Bus Architecture (`VirtualDeviceBus`)
- **Address Routing:** Fast lookup over registered devices based on base address and region size.
- **Collision Detection:** Any overlap during registration causes immediate rejection.
- **Access Sizing:** Supports 1, 2, 4, and 8 byte reads and writes. Misaligned access (e.g. 4-byte read at address not aligned to 4) is rejected with `VirtualDeviceAccessResult::MisalignedAccess`.
- **Interrupt Integration:** Each registered device can signal interrupts to `SmpInterruptRouter`. The bus routes these to target cores as Shared Peripheral Interrupts (SPI).

## 3. Base Device Contract (`VirtualDevice`)
Every virtual device inherits from `VirtualDevice` and implements:
- `read_register(offset, size, out_value)`
- `write_register(offset, size, value)`
- `reset()`
- `serialize_state()`
- `restore_state(bytes)`

## 4. Determinism Invariants
- Devices must not spawn OS threads.
- Devices must not perform asynchronous host system calls.
- Device state must be completely reconstructible from snapshot bytes and replay event sequences.
