# Stage 196.14 — Guest Kernel Memory Allocator Concurrency (Acceptance Tests K & L)

## 1. Architectural Implementation
- **PhysicalPageManager (Acceptance Test K):**
  - Thread-safe bitmap allocator for 16KB physical memory pages.
  - Synchronized via `std::recursive_mutex mutex_` guarding `initialize`, `reserve_range`, `allocate_pages`, `free_pages`, `is_page_allocated`, and `get_stats`.
  - Invariants:
    - Double-free rejection: calling `free_pages` on unallocated or out-of-bounds pages returns `false`.
    - Bitmap consistency across 8 concurrent execution contexts.
    - Full reclamation of free pages upon thread termination.
  - Guest Execution Routine:
    - Emits standalone ARM64 EL1 routine (`emit_guest_alloc_routine`) scanning bitmap in guest RAM, allocating 1 bit, and returning allocated page index in X0.
- **KernelHeapAllocator (Acceptance Test L):**
  - First-fit kernel heap allocator with 16-byte alignment, 16-byte block headers, and bidirectional boundary-tag coalescing.
  - Synchronized via `std::recursive_mutex mutex_` guarding `initialize`, `kmalloc`, `kfree`, `krealloc`, `kcalloc`, `get_stats`, and `validate_heap_integrity`.
  - Invariants:
    - Zero active allocations remaining after all allocations freed.
    - Forward and backward block coalescing completely merges fragmented heap blocks back into a single contiguous free block equal to total heap size.
    - Heap integrity validation traversing header magic and block bounds succeeds under high concurrency.

## 2. Test Evidence (Acceptance Tests K & L)
Verified via `stage196_14_allocator_concurrency_tests`:
- Test K1: 8 concurrent threads executing 800 page allocations and frees on 16MB managed pool. Zero corruption, double-free prevention verified, and all 1024 pages restored to free list.
- Test L1: 8 concurrent threads executing 960 heap operations (`kmalloc`, `kcalloc`, `krealloc`, `kfree`). Full heap coalescing and zero fragmentation verified.
- Test K2: Guest ARM64 EL1 routine execution allocating physical page index 1 from guest memory bitmap.

## 3. Evidence Classification
`GUEST_SMP`, `GUEST_EL1`, `SYNTHETIC_KERNEL`. Tested in Debug and Release.
