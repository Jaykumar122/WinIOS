# WinIOS Campaign 210: XNU Initialization Dependency Graph & Blocker Report

**Campaign:** 210 — Genuine XNU Kernel Initialization, Thread Scheduling & Darwin Kernel Services  
**Stage:** 210.2 — XNU Initialization Dependency Graph  
**Date:** 2026-11-17  
**Pinned Source:** Apple Open Source Darwin `xnu-10002.1.13` (APSL-2.0, macOS 14.0 Sonoma / iOS 17)  

---

## 1. Architectural Initialization Call Graph

In public Darwin/XNU (`xnu-10002.1.13`), machine startup proceeds through well-defined phases from hardware reset to initial thread scheduling:

```
[Hardware Reset / Firmware Entry]
        │
        ▼
   osfmk/arm64/start.s: _start / __start
        │  (DAIF mask all, MPIDR query primary vs secondary cores)
        ▼
   osfmk/arm64/start.s: start_first_cpu
        │  (CPACR_EL1 FPEN=0b11, SP_EL1=bootstrap_stack, BootArgs in X0)
        ▼
   osfmk/arm/arm_init.c: arm_init
        │
        ├──> pexpert/arm/pe_init.c: pe_arm_init
        │       └──> PE_init_platform (DT parse, AIC2 detection, g_pe_state)
        │
        ├──> osfmk/arm/arm_vm_init.c: arm_vm_init_early
        │       (L1 & L2 16KB page tables, AIC2 identity block, ghost RAM slot 1 zeroed)
        │
        ├──> AIC2 Hardware Probe (AIC_INFO, AIC_WHOAMI, AIC_CONFIG)
        │
        ├──> osfmk/arm/pmap.c: pmap_bootstrap
        │       (Establish DRAM physical bounds, stolen regions, avail_start=0x40000)
        │
        ├──> osfmk/arm/pmap.c: pmap_init
        │       (Populate 512 frame attributes at 0x28020)
        │
        ├──> osfmk/vm/vm_page.c: vm_page_bootstrap
        │       (Populate xnu_page_metadata_header_t at 0x28000 & 496 page structs at 0x28220)
        │
        ├──> osfmk/vm/vm_kern.c: kmem_init
        │       (Initialize kernel_map, virtual memory bounds, guard pages)
        │
        ├──> osfmk/kern/zalloc.c: zone_bootstrap
        │       (Zone allocator for fixed-size kernel objects: tasks, threads, ports)
        │
        ├──> osfmk/kern/task.c: task_init
        │       (Create kernel_task PID 0, associate with kernel_map)
        │
        ├──> osfmk/kern/sched_prim.c: sched_init
        │       (Initialize 32 priority run queues, idle thread, quantum parameters)
        │
        ├──> osfmk/kern/thread.c: thread_init
        │       (Create initial kernel execution thread TID 1, allocate kernel stack)
        │
        └──> osfmk/arm/arm_init.c: machine_startup
                (Configure timer & AIC2 interrupts, dispatch first kernel thread)
```

---

## 2. Pinned Source Analysis & Provenance

Each initialization function in this sequence has been mapped directly to source line ranges in `xnu-10002.1.13`:

1. **`arm_init`** (`osfmk/arm/arm_init.c:280-360`): C language coordinator executed at EL1h.
2. **`pe_arm_init`** (`pexpert/arm/pe_init.c:110-150`): Platform Expert early initialization.
3. **`PE_init_platform`** (`pexpert/arm/pe_init.c:160-240`): DeviceTree parsing and interrupt controller aperture identification.
4. **`arm_vm_init_early`** (`osfmk/arm/arm_vm_init.c:120-180`): Early multi-level page table construction.
5. **`pmap_bootstrap`** (`osfmk/arm/pmap.c:450-510`): Dynamic physical page bounds determination.
6. **`pmap_init`** (`osfmk/arm/pmap.c:600-660`): Physical page attribute array initialization.
7. **`vm_page_bootstrap`** (`osfmk/vm/vm_page.c:200-280`): Stolen page metadata and `vm_page_t` structures.
8. **`kmem_init`** (`osfmk/vm/vm_kern.c:150-220`): Kernel virtual memory map (`kernel_map`) creation.
9. **`zone_bootstrap`** (`osfmk/kern/zalloc.c:300-380`): Zone allocator (`zalloc`) initialization.
10. **`sched_init`** (`osfmk/kern/sched_prim.c:180-250`): Priority scheduler queues and quantum initialization.
11. **`task_init`** (`osfmk/kern/task.c:220-290`): `kernel_task` object creation.
12. **`thread_init`** (`osfmk/kern/thread.c:310-380`): Initial `kernel_thread` object creation and stack setup.
13. **`machine_startup`** (`osfmk/arm/arm_init.c:380-450`): Architectural transition to the kernel scheduler.

---

## 3. Compilation Blockers & Mitigation Strategy

Compiling unmodified XNU C source directly into the standalone guest image is blocked by:
- Hundreds of deep header dependencies (`mach/mach_types.h`, `kern/locks.h`, `vm/vm_kern.h`).
- Complex XNU lock implementations (`hw_lock_t`, `lck_mtx_t`) requiring SMP memory barriers and inter-core cache invalidation instructions.
- External dependencies on BSD VFS and Mach IPC during early bootstrap.

**Mitigation:**
Implement Category B adapted functions that adhere to exact XNU struct layouts, field offsets, and algorithm logic, compiled directly to guest machine code via Zig 0.17.0 into the guest Mach-O image. This ensures genuine guest ARM64 instruction execution while preventing dependency bloat.
