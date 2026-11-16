# Stage 205.19 Engineering Report: Early XNU Initialization Compatibility

## 1. Executive Summary
Stage 205.19 verified the early Darwin/XNU kernel initialization contracts, focusing on bootstrap pagetables, translation granule parameters, virtual memory initialization boundaries (`topOfKernelData`), DeviceTree handoff semantics, and W^X kernel security invariants.

## 2. Key Architecture Contracts Verified
1. **DeviceTree Platform Handoff:**
   - Evaluated Apple DeviceTree binary structure against XNU `PE_init_platform()` expectations.
   - Verified `/` root properties: `model` ("iPhone15,4"), `target-type` ("D37AP"), `board-id` (`0x18`), `chip-id` (`0x8120`).
   - Verified `/chosen` node attributes: `boot-args`, `firmware-version`, `dram-base`, `dram-size`.
   - Verified `/cpus` node hierarchy declaring 2 performance cores (`apple,everest`) and 4 efficiency cores (`apple,sawtooth`) matching the physical Apple A16 Bionic configuration.
2. **Translation Control Register (TCR_EL1) 16KB Granule:**
   - Validated bitfield layout for 16KB translation granule: `TG0 = 2` (bits [15:14] = 0b10), `TG1 = 1` (bits [31:30] = 0b01).
   - Validated size fields `T0SZ` and `T1SZ` matching Darwin 48-bit / 39-bit virtual address spaces.
   - Enforced 16KB page alignment on kernel text, data, stack, heap, and direct physical map regions.
3. **Memory Steal Boundaries (`pmap_steal_memory`):**
   - Verified that physical allocations via `EarlyPhysicalPageAllocator` strictly commence above `topOfKernelData`.
   - Confirmed allocations are bounded by physical DRAM limit (`physBase + memSize`).
   - Confirmed bootstrap pagetable allocations and per-CPU stack allocations maintain 16KB page alignment and zero-fill guarantees.
4. **W^X Memory Security Invariants:**
   - Confirmed `KernelMemoryLayout::check_wx_policy()` enforcement:
     - `kKernelTextBase`: Executable, Non-Writable (`R-X`).
     - `kKernelDataBase`: Writable, Non-Executable (`RW-`).
     - `kKernelHeapBase`: Writable, Non-Executable (`RW-`).
     - Guard pages configured with zero access permissions.

## 3. Test Verification
All unit tests in `tests/unit/stage205_19_xnu_early_init_compatibility_tests.cpp` passed with zero errors.
