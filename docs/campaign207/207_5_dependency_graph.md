# Stage 207.5: arm_init() Dependency Graph

## 1. Executive Summary
- **Stage:** 207.5 — arm_init() Dependency Graph
- **Status:** COMPLETE / VERIFIED
- **Date:** 2026-11-16
- **Target Function:** `arm_init(boot_args *args)` in Apple XNU `osfmk/arm/arm_init.c`

---

## 2. Dependency Graph

```mermaid
graph TD
    A["_start (osfmk/arm64/start.s)"] -->|Passes boot_args* in X0, Stack in SP| B["arm_init(boot_args *args)"]
    
    subgraph "Hardware & Host Prerequisites"
        H1["ARM64 CPU: EL1h, DAIF=0xF, CPACR.FPEN=3"]
        H2["Guest DRAM: 0x800000000 (1 GB)"]
        H3["Apple AIC2 Aperture: 0x23B100000"]
        H4["UART Console Buffer: 0x90000"]
    end
    
    subgraph "Boot Data Structures"
        D1["AppleBootArgsRev2 (736 bytes)"]
        D2["DeviceTree Blob at deviceTreeP"]
        D3["topOfKernelData Bump Allocator Pointer"]
    end
    
    subgraph "arm_init Execution Sequence"
        B --> C1["pe_arm_init(args)"]
        C1 --> C2["PE_init_platform(FALSE, args)"]
        C2 --> C3["DeviceTree Parser (/chosen, /arm-io, /memory-map)"]
        C2 --> C4["Console Init (PE_create_console -> UART)"]
        
        B --> M1["arm_vm_init(args->memSize, args)"]
        M1 --> M2["Bump-allocate 16KB Page Table Root from topOfKernelData"]
        M1 --> M3["Populate L1/L2 Descriptors (W^X Compliant)"]
        M1 --> M4["Set MAIR_EL1 & TCR_EL1 (16KB Granule, T0SZ=25, T1SZ=25)"]
        M1 --> M5["Install TTBR0_EL1 & TTBR1_EL1"]
        
        B --> P1["AIC2 Interrupt Controller Probe"]
        P1 --> P2["Read AIC_INFO (0x23B100004) -> 0x41494332"]
        P1 --> P3["Read AIC_WHOAMI (0x23B102000) -> 0"]
        P1 --> P4["Write AIC_CONFIG (0x23B100010) -> 1"]
        
        B --> T1["timer_init() -> CNTP/CNTV Counter Validation"]
        B --> S1["Write Completion Mailbox (0xCAFEBABE207)"]
        B --> S2["Architectural Halt: WFI"]
    end

    H1 --> B
    H2 --> B
    H3 --> P1
    H4 --> C4
    D1 --> B
    D2 --> C3
    D3 --> M2
```

---

## 3. Detailed Structural Dependencies

| Dependency | Class | Specification | Location |
| :--- | :--- | :--- | :--- |
| `boot_args` | Struct | `AppleBootArgsRev2` (736 B, naturally aligned) | `0x20000` / `args->physBase + 0x4000` |
| `deviceTreeP` | Blob | Flattened Apple DeviceTree (`AppleDeviceTreeBuilder`) | `args->deviceTreeP` |
| `physBase` | Memory | Physical DRAM base address | `0x800000000` (or `0x0` depending on profile) |
| `topOfKernelData` | Allocator | Physical boundary beyond loaded image & stack | Used for initial page tables & mailbox |
| `AppleAIC2` | MMIO | Base `0x23B100000`, 1 MB window | Attached to `CPU` virtual memory bus |
| `UART` | MMIO | Base `0x90000`, PL011/S5L compatible | Memory-mapped character FIFO |
| `SP` | Register | 16-byte aligned kernel bootstrap stack | 16 KB memory range (`stack_top`) |
| `FP` / `LR` | Registers | Standard AAPCS64 call frame registers (`X29`, `X30`) | Preserved across calls |
