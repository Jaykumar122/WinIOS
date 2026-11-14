# Campaign 194 Stage 194.16: Kernel Device-Driver Interface & MMIO

## Objective
Provide driver abstraction and device I/O integration for the synthetic kernel runtime:
1. `DeviceDriver` & `DriverManager`:
   - Abstract `DeviceDriver`: device identifiers (`device_id`, `name`, `mmio_base`, `mmio_size`, `irq_number`) and operations (`probe()`, `initialize()`, `read_mmio()`, `write_mmio()`, `handle_irq()`).
   - `SyntheticUartDriver`: Authentic PL011 register layout (DR, FR, CR, ICR) at default MMIO address `0x09000000`, IRQ 33, transmit and receive FIFO queues, and console output buffering.
2. MMIO mapping & Memory bus integration:
   - Registers MMIO regions in guest physical memory map.
   - Routes physical address reads/writes to registered drivers.
   - Routes hardware interrupts (IRQs) to registered driver handlers.

## Implementation Details
- Created `src/kernel/driver_manager.h` and `src/kernel/driver_manager.cpp`:
  - `DeviceDriver` abstract base interface.
  - `SyntheticUartDriver` wrapping `VirtualUartDevice` for PL011 console I/O, character output, string writing, and input injection.
  - `DriverManager` handling driver registration, probing, MMIO address translation, and IRQ dispatch.
- Authored test suite `tests/unit/stage194_16_device_drivers_tests.cpp`.

## Test Results
- `stage194_16_device_drivers_tests`:
  - `test_driver_registration_and_lifecycle`: PASSED (registration, probe, and initialization).
  - `test_mmio_register_read_write`: PASSED (PL011 Flag Register read, Data Register writes).
  - `test_high_level_console_io`: PASSED (string writing and console buffer capture).
  - `test_input_injection_and_reading`: PASSED (input string injection and byte-by-byte DR reading).
  - `test_driver_irq_dispatch`: PASSED (IRQ 33 routing to UART driver interrupt handler).
  - Total: 31 passed, 0 failed.

## Evidence Classification
- Device MMIO & Driver Abstraction: `SYNTHETIC_KERNEL`
