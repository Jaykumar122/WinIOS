# Stage 196.0 — Baseline, Repository & Evidence Audit

## 1. Repository State & Verification
- **Repository Root:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Baseline Git Commit:** `435e67135c2012074fb3ea1aa06029e4931c5921` (Campaign 195 closeout)
- **Host OS:** Windows 11 Professional x64 (Version 10.0.26100)
- **Primary Toolchain:** Microsoft Visual Studio 18 2026 (MSBuild 18.10.1), MSVC C++20 (`/W4 /WX /permissive-`)
- **Build System:** CMake 4.4.4, Ninja / Visual Studio generators
- **Cross-Compiler:** Zig 0.17.0 (`zig cc -target aarch64-macos`, `zig cc -target aarch64-freestanding`)
- **Submodule Pin:** `D:\WinISO26\Inferno-Research\Inferno` pinned at `cc4302a99167abec69b714cfd00c38caece7e7de`
- **Protected Files:**
  - `D:\WinISO26\WinIOS_README.md`: SHA256 `61f4d078d5071b5ab16093eb5418ed15afb7ed41cd3c84f080ce44e196ea7fef` (Preserved intact)
  - `D:\WinISO26\WinIOS\docs\progress.md`: SHA256 `447e7b5ab74b9331cbe618bea6561aba83c4d291dc5ccc16df1d72023c462212` (Preserved intact)
  - `D:\WinISO26\WinISO26_README.md`: (Not present, preserved)
- **Untracked / Modified Files at Entry:** Only `.gitignore` updated to track `docs/campaign196/`.

## 2. Baseline Test Status
- **Total Registered Test Targets:** 391
- **Passed:** 390
- **Skipped:** 1 (`inferno_integration_tests` due to standalone execution mode without live network/external Inferno daemon)
- **Failed:** 0

## 3. Audit of Existing Synchronization & Concurrency Subsystems
1. **ARM64 Exclusive Monitor (`src/cpu/smp_exclusive_monitor.h`, `src/cpu/smp_exclusive_monitor.cpp`):**
   - Implements a 64-byte reservation granule (`kGranuleSize = 64`), matching Apple A16 Bionic architectural cache line size.
   - Core reservation entries track `core_id`, `granule_address`, `exact_address`, `size`, `valid`.
   - Invalidation on write (`invalidate_reservations_containing`) is hooked into memory writes when SMP cluster is active.
   - Gaps to audit: `LDXP`/`STXP`, double-word exclusive handling, cross-core contention under high frequency, and reservation clear on exception/ERET.
2. **Atomic Instructions (`src/cpu/cpu.cpp`):**
   - Single-word `CAS` / `CASA` / `CASL` / `CASAL` is partially decoded.
   - LSE atomics (`SWP`, `LDADD`, `LDCLR`, `LDEOR`, `LDSET` families) and pair CAS (`CASP`) need complete architectural decoding and validation under SMP.
3. **Barriers (`DMB`, `DSB`, `ISB` in `src/cpu/cpu.cpp`):**
   - Basic NOP or coarse memory barrier emulation exists. Option fields (SY, ISH, ISHST, ISHLD, NSH, OSH) need strict decoding, verification, and modeling.
4. **Kernel Spinlock (`src/kernel/spinlock.h`, `src/kernel/spinlock.cpp`):**
   - Implements ticket or test-and-set spinlock with owner tracking, recursion detection, and lock order validation.
   - Requires audit of IRQ-safe acquisition (`acquire_irqsave` / `release_irqrestore`) to prevent deadlock from timer preemption while holding a spinlock.
5. **WaitQueue & Scheduler (`src/kernel/wait_queue.h`, `src/kernel/smp_scheduler.h`):**
   - Wait queue supports thread sleep and wake-up. Need to audit against race conditions (missed wake-up if unblock arrives before block completes).
   - Scheduler state machine (`READY`, `RUNNING`, `BLOCKED`, `TERMINATED`) requires strict transition assertions and cross-core migration safety up to 8 virtual cores.
6. **Memory Allocators & Demand Paging (`src/kernel/physical_page_manager.h`, `src/kernel/kernel_heap_allocator.h`):**
   - Mutex-protected or lock-free allocations. Need stress testing under concurrent multi-core demand paging and heap allocation.

## 4. Evidence Classification Boundary
All tests and observations are categorized strictly:
- `HOST_MODEL`: Verification of emulation logic on Windows host.
- `GUEST_EL0` / `GUEST_EL1`: Real ARM64 guest instructions executed in virtual CPU.
- `GUEST_ATOMIC`: Real ARM64 atomic instructions (`LDXR`/`STXR`, `CAS`, `SWP`, `LDADD`, etc.) executed by guest.
- `GUEST_MEMORY_ORDER`: Memory barrier and litmus test observations.
- `GUEST_SMP`: Concurrent execution across multiple virtual CPU cores.
- `SYNTHETIC_KERNEL`: Synthetic WinIOS kernel environment modeling Darwin/XNU semantics (NEVER claimed as genuine Apple XNU kernel).
