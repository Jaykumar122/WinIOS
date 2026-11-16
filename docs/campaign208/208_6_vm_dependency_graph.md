# WinIOS Stage 208.6: XNU VM Bootstrap Dependency Graph

## 1. Pinned Source Dependency Chain
Analysis of Apple `xnu-10002.1.13` reveals the following sequential call graph during Darwin/XNU machine-dependent startup on Apple Silicon:

```mermaid
flowchart TD
    A["_start (osfmk/arm64/start.s)"] --> B["arm_init(boot_args *args) (osfmk/arm64/arm_init.c)"]
    B --> C["pe_arm_init(args) (pexpert/arm/pe_bootargs.c)"]
    C --> D["PE_init_platform(FALSE, args) (pexpert/arm/pe_init.c)"]
    B --> E["arm_vm_init(args->memSize, args) (osfmk/arm64/arm_vm_init.c)"]
    E --> E1["arm_vm_page_granular_prot_setup()"]
    E --> E2["early page-table stealer: alloc bootstrap tables from topOfKernelData"]
    E --> E3["set TCR_EL1 (TG0=16KB, TG1=16KB, T0SZ, T1SZ)"]
    E --> E4["set MAIR_EL1 & TTBR0_EL1/TTBR1_EL1"]
    E --> E5["msr SCTLR_EL1, M=1 (activate MMU)"]
    B --> F["cpu_init() (osfmk/arm64/cpu.c)"]
    B --> G["pmap_bootstrap(args->memSize, args) (osfmk/arm64/pmap/pmap.c)"]
    G --> G1["vm_page_bootstrap() (osfmk/arm64/pmap/pmap.c)"]
    G --> G2["Initialize avail_start, avail_end, first_avail physical page bounds"]
    B --> H["machine_startup() (osfmk/arm/machine_routines.c)"]
```

## 2. Analysis of Next Initialization Milestones
Following `PE_init_platform`, genuine XNU requires:
1. **`arm_vm_init` expansion:**
   - Real 16KB translation table hierarchy.
   - Stealing multiple 16KB pages above `topOfKernelData` for Level 1 table, Level 2 tables, and Level 3 page tables.
   - Configuring `TCR_EL1` for 16KB translation granule (`TG0 = 2`, `TG1 = 1`).
   - Configuring `MAIR_EL1` with Normal memory and Device-nGnRE memory attributes.
   - Configuring `TTBR0_EL1` (Identity map) and `TTBR1_EL1` (Higher-half kernel VA).
2. **`pmap_bootstrap` invocation:**
   - Physical memory map registration (`avail_start`, `avail_end`, `first_avail`).
   - Tracking allocated physical page frames.
   - Ensuring `topOfKernelData` advances deterministically past page tables.

## 3. Unresolved Prerequisites & Boundary Identification
- Full `machine_startup` requires Mach IPC task creation, BSD VFS mount, and Mach scheduler threads.
- For Stage 208, the achievable and verifiable genuine guest execution boundary is:
  `_start` $\rightarrow$ `arm_init` $\rightarrow$ `pe_arm_init` $\rightarrow$ `PE_init_platform` $\rightarrow$ `arm_vm_init` (with real 16KB multi-level tables and MMU enable) $\rightarrow$ `pmap_bootstrap` (physical memory bounds registration).
