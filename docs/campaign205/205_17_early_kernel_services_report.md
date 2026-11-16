# Stage 205.17 Engineering Report: Early Kernel Runtime Services

## 1. Executive Summary
Stage 205.17 implemented and validated the early kernel runtime services required by early Darwin/XNU kernel initialization before high-level subsystems (virtual memory manager, Mach IPC, scheduler) come online. This includes:
- **`EarlyUartConsole`**: Direct hardware-mapped UART console (base `0x90000`) providing raw character and string emission with in-memory log buffer capture for diagnostics.
- **`EarlyPhysicalPageAllocator`**: 16KB-page physical memory stealer mimicking Darwin's `pmap_steal_memory()`, with alignment controls, transactional watermark tracking, boundary validation, and zero-fill guarantees.
- **`EarlyMonotonicTimer`**: 24 MHz counter tick and microsecond translation provider mirroring ARM64 `CNTPCT_EL0` / `CNTVCT_EL0` timebase.
- **`EarlyKernelServicesManager`**: Unified runtime facade orchestrating early services.

## 2. Component Implementation Details
- **UART Console**: Supports physical write at `base_addr + offset` and safe buffer recording even when target physical memory is out-of-bounds.
- **Physical Page Allocator**: Enforces strict 16KB granule alignment (`kPageSize16K = 0x4000`), custom power-of-two alignment (e.g. 64KB for early translation tables), zero-initialization, and bounds checking preventing watermark overruns.
- **Monotonic Timer**: Converts microseconds to ticks with formula `(us * 24000000) / 1000000`, matching Apple platform frequency definitions.

## 3. Test Verification
All unit tests in `tests/unit/stage205_17_early_kernel_services_tests.cpp` passed with zero errors:
1. `EarlyUartConsole`: Verified single character emission, string emission, in-memory log buffer consistency, log clearing, and out-of-bounds safety.
2. `EarlyPhysicalPageAllocator`: Verified 16KB page allocation, 64KB aligned page allocation, memory zero-fill across allocated pages, watermark progression, and out-of-memory rejection.
3. `EarlyMonotonicTimer`: Verified tick calculations at 24 MHz, microsecond translation, and tick advancement.
4. `EarlyKernelServicesManager`: Verified integrated facade operation across UART, Allocator, and Timer.
