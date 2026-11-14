# Stage 195.16 — Kernel Heap & Physical Allocator Stress

## Subsystem Overview
- **Component**: Physical Page Allocator (16KB page frames) & Kernel Dynamic Heap Allocator (`kmalloc` / `kfree` / `krealloc` / `kcalloc`)
- **Architectural Classification**: `GUEST_EL1` / `SYNTHETIC_KERNEL`
- **Device Research Target**: iPhone 15 (`iPhone15,4` / `D37AP` / `T8120`), Apple A16 Bionic (`T8120`)

---

## 1. Objectives & Stress Verification
Stage 195.16 validates the physical page allocator and kernel heap allocator under high-volume stress, edge-case fragmentation, boundary conditions, and concurrent memory lifecycle patterns:

1. **High-Volume Physical Page Cycles**:
   - 600 interleaved allocation and free operations using deterministic pseudo-random sequences.
   - Exact tracking and verification between `ppm.get_stats()` counters and raw guest RAM bitmap bits (`get_bit` vs memory byte scan).
2. **Fragmentation, Exhaustion & Contiguity Constraints**:
   - Complete exhaustion of 64 managed pages down to 0 free pages; verify allocation past exhaustion returns 0 without corruption.
   - Checkerboard fragmentation (freeing every even-indexed page, leaving 32 isolated 16KB slots).
   - Multi-page contiguous request (`allocate_pages(2)`) safely fails due to lack of adjacent slots; single-page allocation succeeds by reclaiming fragmented holes.
   - Full reclamation and reallocation of large contiguous 32-page blocks.
3. **Double-Free & Out-of-Bounds Rejection**:
   - Hardened `PhysicalPageManager::free_pages` to verify that every page in the requested range was actually marked allocated (`get_bit`) before freeing. Double-free attempts return `false` without corrupting state.
   - Rejection of out-of-range base addresses, addresses exceeding managed size, and misaligned physical addresses.
4. **Kernel Heap Mixed-Size Batches & 16-Byte Alignment**:
   - 100 mixed-size allocations spanning 16B, 32B, 64B, 128B, 256B, 512B, 1024B, 2048B, and 4096B.
   - 100% strict adherence to 16-byte alignment (`ptr % 16 == 0`).
   - Distinct canary cookies written across all 100 payloads; 50 interleaved frees executed; remaining 50 payloads verified byte-for-byte uncorrupted.
5. **Bidirectional Free-Block Coalescing & Realloc Integrity**:
   - Enhanced `KernelHeapAllocator::kfree` with backward (preceding block) and forward (succeeding block) free-block merging.
   - Sequential allocations A, B, C, D, E with frees of B, D, C successfully coalesce into a single 1072-byte contiguous hole, allowing immediate allocation of a 1000-byte block starting at B.
   - Validated `krealloc` expansion (64B -> 256B) and contraction (256B -> 32B) preserving payload data.

---

## 2. Test Execution & Results
Executable: `stage195_16_allocator_stress_tests.exe`

| Test Name | Operations & Invariants | Observations | Status |
|:---|:---|:---|:---|
| `test_physical_allocator_high_volume_stress` | 600 pseudo-random alloc/free ops on 448 pages | Bitmap bit count exactly matched active allocations; zero memory leaks after cleanup | **PASSED** |
| `test_physical_allocator_fragmentation_and_exhaustion` | 64-page exhaustion, 50% checkerboard fragmentation | 2-page contiguous alloc rejected; single page reclaimed; full span allocated upon recovery | **PASSED** |
| `test_physical_allocator_rejection` | Double-free, out-of-bounds, unaligned addresses | All invalid requests cleanly rejected with `false` | **PASSED** |
| `test_kernel_heap_mixed_size_stress` | 100 allocations (16B-4096B), canary validation | 16-byte alignment verified on all blocks; canary cookies completely intact across interleaved frees | **PASSED** |
| `test_kernel_heap_coalescing_and_realloc` | Bidirectional coalescing (B+C+D), `krealloc` | Combined 1072B hole satisfied 1000B allocation; data preserved across expansion & contraction | **PASSED** |

---

## 3. Compliance and Sign-Off
- Zero compiler warnings under MSVC `/W4 /WX /permissive-`.
- 100% of allocator stress tests passed.
- Both physical and kernel heap allocators verified robust under fragmentation and boundary pressure.
