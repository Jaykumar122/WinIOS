# WinIOS Campaign 205 — Stage 205.1 Engineering Report
## Public XNU Source and ARM64 Boot Architecture Research

**Date:** November 16, 2026  
**Campaign:** 205  
**Stage:** 205.1  
**Target:** Darwin / XNU Operating System Kernel (Apple Silicon / ARM64 / ARM64e)  
**Research Reference:** Public Apple Open Source XNU (e.g. `xnu-8792.81.2`, `xnu-10002.1.13`, `xnu-10063.121.3`), Apple Platform Expert (`pexpert`), DarwinOS  

---

### 1. Public XNU Source Layout & Provenance
Public XNU releases are published by Apple under the Apple Public Source License (APSL 2.0). Key architectural components relevant to early ARM64 boot include:
- **`osfmk/arm64/start.s`**: The ARM64 assembly entry point (`_start`), responsible for initial CPU register validation, early page table mapping, MMU activation, and jump into C code.
- **`osfmk/arm64/arm_init.c`**: Primary C entry point (`arm_init(boot_args *args)`), which coordinates early virtual memory setup, platform expert initialization, exception vector installation, and subsystem bringup.
- **`pexpert/pexpert/arm64/boot.h`**: Binary ABI specification for `struct boot_args` (Revision 2, Version 2, 736 bytes in LP64).
- **`pexpert/pexpert/device_tree.h`**: Device tree representation and traversal functions (`DTInit`, `SecureDTFindNode`).
- **`osfmk/arm64/locore.s`**: Low-level exception vector table (`ExceptionVectorsBase`), interrupt handlers, context switching, and system register manipulation.
- **`osfmk/arm64/vm/`**: Translation table management (Stage-1 TTBR0/TTBR1), 16KB granule address translation, page table allocation, and KASLR slide handling.

---

### 2. ARM64 Kernel Entry & Early Initialization Sequence

```
+---------------------------------------------------------------------------------------+
| Bootloader / Firmware Handoff                                                         |
|   - CPU in AArch64 EL1 (or EL2 hypervisor)                                            |
|   - MMU disabled or identity-mapped; D-Cache clean; I-Cache invalidated               |
|   - X0 = physical address of boot_args structure                                      |
+---------------------------------------------------------------------------------------+
                                           |
                                           v
+---------------------------------------------------------------------------------------+
| osfmk/arm64/start.s: _start                                                           |
|   1. Save boot_args pointer (MOV X19, X0)                                             |
|   2. Clear BSS / early scratch space                                                  |
|   3. Build bootstrap identity & high-virtual translation tables                       |
|   4. Configure System Control: MAIR_EL1, TCR_EL1 (16KB granule, 48-bit VA)            |
|   5. Program TTBR0_EL1 and TTBR1_EL1                                                  |
|   6. Enable MMU and caches in SCTLR_EL1 (M=1, C=1, I=1)                               |
|   7. ISB; transition to high-kernel virtual address space                             |
|   8. Set stack pointer (SP_EL1 = &bootstrap_stack)                                   |
|   9. BL arm_init (passing virtual boot_args pointer)                                  |
+---------------------------------------------------------------------------------------+
                                           |
                                           v
+---------------------------------------------------------------------------------------+
| osfmk/arm64/arm_init.c: arm_init(boot_args *args)                                     |
|   1. PE_init_platform: Parse boot arguments (command line, boot flags, memory bounds) |
|   2. PE_init_devicetree: Ingest Apple Flattened Device Tree / FDT                     |
|   3. PE_init_kprintf: Initialize early UART / framebuffer console                     |
|   4. arm_vm_init: Initialize full virtual memory subsystem (pmap, zones, physical RAM)|
|   5. exception_init: Install architectural exception vectors into VBAR_EL1            |
|   6. PE_init_interrupts: Discover interrupt controller (AIC / AIC2 / GICv3)          |
|   7. time_init: Initialize Apple system timer / generic ARM timer (CNTVCT_EL0)       |
|   8. cpu_init: Identify CPU topology, performance/efficiency cores, features          |
|   9. kernel_bootstrap: Transfer control to Mach scheduler and kernel task             |
+---------------------------------------------------------------------------------------+
```

---

### 3. Key Boot Interfaces & Data Structures

#### 3.1 `struct boot_args` (Revision 2, Version 2)
As defined in `pexpert/arm64/boot.h` and implemented in WinIOS `src/firmware/apple_boot_args.h`:
- `Revision`: `2`
- `Version`: `2`
- `virtBase`: High virtual base address of kernel text mapping (typically `0xFFFFFE0007004000ULL` on modern Apple Silicon or `0xFFFFFFF007004000ULL` on legacy).
- `physBase`: Physical RAM start address (e.g., `0x800000000ULL`).
- `memSize`: Size of available physical RAM.
- `topOfKernelData`: Physical ceiling address of loaded kernel segments, devicetree, and bootloader data.
- `Video`: `AppleBootVideo` framebuffer descriptor (baseAddr, display, rowBytes, width, height, depth).
- `deviceTreeP`: Physical pointer to Apple Flattened Device Tree.
- `deviceTreeLength`: Size in bytes of device tree blob.
- `CommandLine`: Null-terminated kernel command line string (max 608 bytes).
- `bootFlags`: Diagnostic flags (`kAppleBootFlagSerial`, `kAppleBootFlagSafeBoot`, etc.).

#### 3.2 Device Tree Interface
Darwin/XNU expects an Apple Device Tree (ADT or FDT variant) containing nodes for:
- `/chosen`: Boot arguments, memory ranges, firmware versions, random seed.
- `/cpus`: CPU core descriptions, topology, cluster identification (P-cores / E-cores).
- `/arm-io`: On-chip peripheral bus registers, interrupt controllers (AIC/GIC), timers, UART/serial console.
- `/memory`: Physical memory banks and reserved memory ranges.

#### 3.3 Virtual Memory Requirements
- **Granule Size:** Modern Apple Silicon XNU requires a **16KB translation granule** (`TCR_EL1.TG0 = 0b10` or `TG1 = 0b10`). WinIOS's native 16KB MMU engine conforms directly to this expectation.
- **Address Space:** TTBR1_EL1 covers high kernel space (`0xFFFFFE0000000000`–`0xFFFFFFFFFFFFFFFF`), while TTBR0_EL1 covers user address space (`0x0000000000000000`–`0x00007FFFFFFFFFFF`).

#### 3.4 Interrupt Controller & Timer
- On physical iPhone 15 (`T8120`), Apple utilizes the proprietary **Apple Interrupt Controller (AIC2)**.
- On Apple Virtualization targets (e.g. `Virtualization.framework` on macOS ARM64), Darwin kernel includes native driver support for standard **ARM GICv3** (`apple,gic-v3`).
- Timer facilities use the standard ARM64 Generic Timer system registers: `CNTP_CTL_EL0`, `CNTP_TVAL_EL0`, `CNTP_CVAL_EL0`, `CNTPCT_EL0`, and `CNTFRQ_EL0`.

---

### 4. Technical Honesty & Scope Constraints
- **Public XNU vs. iPhone 15 Production Firmware:** Public XNU source code releases do NOT contain proprietary Apple Secure Boot chain components (SEP, SecureROM, LLB, iBoot cryptographic verification).
- **Driver Model (IOKit):** Closed-source proprietary drivers for display co-processors, neural engines, baseband, and cellular silicon are not available in public XNU source.
- **Kernel Boot Milestones:** A boot pipeline that initializes CPU state, sets up page tables, passes valid `boot_args`, enters `_start`, and executes real kernel instructions represents genuine operating system execution, regardless of whether peripheral drivers reach userspace.
