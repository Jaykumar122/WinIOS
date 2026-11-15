# WinIOS Campaign 198: Guest Workload Execution Report

**Target Research Architecture:** ARMv8.1-A / ARM64e (iPhone 15 / Apple A16 Bionic `T8120`)  
**Cross-Compiler Toolchain:** Zig 0.17.0 (`aarch64-macos`, freestanding Mach-O)  
**Classification:** `GUEST_MEMORY_MODEL` / `GUEST_CACHE` / `GUEST_DMA`  

---

## 1. Executive Summary

Campaign 198 Stage 17 demonstrates the execution of real, freestanding ARM64 Mach-O binaries exercising data cache zeroing (`DC ZVA`), self-modifying code synchronization (`DC CVAU` -> `DSB ISH` -> `IC IVAU` -> `DSB ISH` -> `ISB`), atomic read-modify-write instructions (`LDADD`, `SWP`), and synthetic DMA queue processing. All operations were verified under the unified memory subsystem.

---

## 2. Workload Artifact Details

### `guest_memory_dma_cache_workload`
- **Source File:** `tests/artifacts/src/guest_memory_dma_cache_workload.S`
- **Target Binary:** `tests/artifacts/bin/guest_memory_dma_cache_workload`
- **SHA-256 Digest:** `a593ff27dad71b862f577fe303127667454d3e20e401023f508cd5336db2475f`
- **Compiler Command:**
  ```cmd
  zig cc -target aarch64-macos -nostdlib -fno-sanitize=undefined -fno-stack-protector "-Wl,-e,_start" "-Wl,-pagezero_size,0" tests/artifacts/src/guest_memory_dma_cache_workload.S -o tests/artifacts/bin/guest_memory_dma_cache_workload
  ```
- **Target Architecture:** ARM64 Mach-O 64-bit executable.

---

## 3. Workload Execution Flow & Architectural Highlights

1. **Cache Zeroing (`DC ZVA`):**
   - The workload clears a 64-byte code buffer at `0x20000` using `dc zva, x1` followed by `dsb ish`.
   - Verified that the cache zero block adheres to `DCZID_EL0.BS = 4` (64 bytes).
2. **Dynamic Code Modification (Self-Modifying Code):**
   - The workload writes two 32-bit instructions into `0x20000`:
     - `0x52803300`: `MOVZ W0, #0x0198`
     - `0xD65F03C0`: `RET`
3. **Architectural Cache Synchronization:**
   - To make newly written instructions visible to the instruction fetch pipeline, the guest executes:
     ```assembly
     dc   cvau, x1   // Clean Data Cache line to Point of Unification
     dsb  ish        // Data Synchronization Barrier
     ic   ivau, x1   // Invalidate Instruction Cache line to PoU
     dsb  ish        // Data Synchronization Barrier
     isb             // Instruction Synchronization Barrier
     ```
   - The guest then executes `blr x1`. The CPU branches to `0x20000`, executes the generated instructions, and returns with `W0 == 0x0198`.
4. **LSE Atomic Memory Integration:**
   - The guest verifies memory synchronization by executing `LDADD` to atomically increment a shared counter by `0x98` (verifying old value returned in register) and `SWP` to store `0x0198C0DE`.
5. **Completion Signature:**
   - Upon successful verification of all phases, the workload writes completion signature `0x1985C0DE` to `0x40000` and enters a quiescent spin loop.
6. **Concurrent DMA Execution:**
   - Concurrent with CPU execution, the DMA engine executes chained fill descriptors populating target physical memory ranges with pattern `0xA16B1026`.

---

## 4. Execution Evidence

The test suite `stage198_17_guest_workload_tests` verified:
- Binary successfully loaded into guest memory via `MachOLoader`.
- Retired all instructions in the workload sequence.
- Verified completion signature `0x1985C0DE` at `0x40000` and swapped value `0x0198C0DE` at `0x40008`.
- 100% test pass with zero regressions.
