# Stage 207.2: XNU Source Revision and Function Mapping

## 1. Upstream Source Specification
- **Pinned Distribution:** Apple Open Source `xnu-10002.1.13`
- **Release Horizon:** macOS Sonoma 14.0 / iOS 17.0 / Darwin 23.0.0
- **License:** Apple Public Source License (APSL) 2.0
- **Upstream Repository:** `https://github.com/apple-oss-distributions/xnu`
- **Architecture Target:** ARM64 / ARM64e (`__arm64__`, `HAS_APPLE_PAC`, Apple A16 Bionic `T8120`)

---

## 2. Machine-Dependent Initialization Sequence Map

```
_start (osfmk/arm64/start.s)
  │
  ├── 1. DAIF Masking (MSR DAIFSet, #0xF)
  ├── 2. Core Affinity (MRS X20, MPIDR_EL1)
  ├── 3. Exception Base (MSR VBAR_EL1, <vector_table>)
  ├── 4. Primary Core Branch (start_first_cpu)
  ├── 5. Stack Setup (LDR SP, =bootstrap_stack_top)
  │
  ▼
arm_init(boot_args *args) (osfmk/arm/arm_init.c)
  │
  ├── 1. Argument Preservation (X0 -> boot_args_storage)
  ├── 2. Platform Expert Early Init
  │     └── pe_arm_init(args) (pexpert/arm/pe_init.c)
  │           └── PE_init_platform() (pexpert/arm/pe_platform.c)
  │                 ├── PE_init_printf(&PE_init_iokit_panic_ui)
  │                 ├── DeviceTree Traversal (args->deviceTreeP)
  │                 │     ├── /chosen -> boot arguments, debug flags
  │                 │     ├── /memory-map -> DRAM regions, reserved ranges
  │                 │     └── /arm-io -> AIC, UART, PMGR, timers
  │                 └── Early UART Console Setup (PE_create_console())
  │
  ├── 3. Early Physical Memory Stealer & VM Setup
  │     └── arm_vm_init(args->memSize, args) (osfmk/arm/arm_vm_init.c)
  │           ├── topOfKernelData bump allocation (ptables, page structs)
  │           ├── Construction of Level 1 & Level 2 16KB translation tables
  │           ├── Enforcement of W^X (Text: RO+X, Data: RW+NX, MMIO: Dev-nGnRnE)
  │           └── TTBR0/TTBR1 & TCR_EL1 / MAIR_EL1 configuration
  │
  ├── 4. CPU Context & Feature Initialization
  │     ├── cpu_data_init(&BootCpuData)
  │     ├── ID Register Probing (ID_AA64ISAR0/1, CTR_EL0, CLIDR_EL1)
  │     └── Architectural FP/SIMD Enablement (CPACR_EL1.FPEN = 0b11)
  │
  ├── 5. Interrupt Subsystem Early Bringup
  │     ├── ml_install_interrupt_handler()
  │     └── Apple AIC2 Controller Initialization (AIC_CONFIG, masks, routing)
  │
  └── 6. Timer Subsystem Initialization
        └── timer_init() -> CNTP / CNTV hardware counter programming
```

---

## 3. Detailed Function Specifications

### 3.1 `_start`
- **Source File:** `osfmk/arm64/start.s`
- **Symbol:** `_start` / `start_first_cpu`
- **Calling Convention / Protocol:** Handoff from iBoot / BootLoader.
  - `X0`: Pointer to physical address of `boot_args` structure.
  - `SP`: Undefined or temporary firmware stack.
  - `PSTATE`: EL1h, interrupts masked (`DAIF = 0b1111`).
  - MMU: Disabled or flat identity-mapped by loader.
- **Responsibilities:**
  - Mask all asynchronous exceptions (`msr daifset, #0xf`).
  - Preserve `X0` across early initialization.
  - Identify primary CPU via `MPIDR_EL1` (Affinity 0..2). Secondary cores branch to parked WFE loop.
  - Set `VBAR_EL1` to early bootstrap exception vectors.
  - Establish C runtime stack (`bootstrap_stack`).
  - Jump to `arm_init`.

### 3.2 `arm_init`
- **Source File:** `osfmk/arm/arm_init.c` (lines 142–395)
- **Signature:** `void arm_init(boot_args *args)`
- **Input Parameters:** `X0` = pointer to canonical `boot_args` structure.
- **Key State Invariants:**
  - Stack pointer `SP` points to valid RAM range (`bootstrap_stack`).
  - Frame pointer `X29` set, `X30` holds return address.
  - BSS segment cleared.
- **Responsibilities:**
  - Store `args` into global `boot_args_storage`.
  - Call `pe_arm_init(args)` to initialize DeviceTree parser and platform callbacks.
  - Call `arm_vm_init(args->memSize, args)` to transition to kernel virtual memory layout.
  - Call `cpu_data_init(&BootCpuData)`.
  - Initialize console logging via `kprintf`.

### 3.3 `PE_init_platform`
- **Source File:** `pexpert/arm/pe_platform.c` (lines 85–240)
- **Signature:** `void PE_init_platform(boolean_t vm_initialized, void *args)`
- **Input Parameters:**
  - `vm_initialized`: `FALSE` during early phase (before MMU transition), `TRUE` after.
  - `args`: Pointer to `boot_args`.
- **Responsibilities:**
  - Walk DeviceTree nodes starting at `args->deviceTreeP`.
  - Locate `/chosen` node and extract boot arguments string and console flags.
  - Identify interrupt controller type (`apple,aic-2` or `apple,aic`).
  - Probe console UART (`apple,s5l-uart` or PL011).
  - Register platform expert function pointers (`PE_poll_input`, `PE_putc`, `PE_halt`).

### 3.4 `arm_vm_init`
- **Source File:** `osfmk/arm/arm_vm_init.c` (lines 90–310)
- **Signature:** `void arm_vm_init(uint64_t memsize, boot_args *args)`
- **Input Parameters:**
  - `memsize`: Total physical RAM size in bytes.
  - `args`: Pointer to `boot_args`.
- **Responsibilities:**
  - Validate physical RAM boundaries: `args->physBase` through `args->physBase + args->memSize`.
  - Bump-allocate early page tables starting at `args->topOfKernelData`.
  - Populate 16KB Level 1 and Level 2 translation descriptors.
  - Set higher-half kernel mapping (`0xFFFFFF8000000000ULL` with `T1SZ=25`).
  - Enforce W^X permissions on all kernel pages.

---

## 4. Register State and ABI Requirements (AAPCS64)
- **General Purpose Registers:**
  - `X0` (`boot_args*`): Input argument 0.
  - `X1`–`X7`: Additional arguments / scratch.
  - `X8`: Indirect result location register.
  - `X9`–`X15`: Caller-saved temporary registers.
  - `X16`–`X17`: Intra-procedure-call scratch registers (IP0, IP1).
  - `X18`: Platform register (reserved by Darwin for `per-cpu` data pointer).
  - `X19`–`X28`: Callee-saved registers.
  - `X29`: Frame Pointer (`FP`).
  - `X30`: Link Register (`LR`).
  - `SP`: Stack Pointer (must maintain 16-byte alignment at all public call boundaries).
- **Floating Point / SIMD:**
  - `CPACR_EL1.FPEN` must be set to `0b11` to prevent `EC = 0x07` (Access to SIMD/FP trapped).
