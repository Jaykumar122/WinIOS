# WinIOS Campaign 200: Cross-Subsystem Integration Contracts

**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`) Virtual Platform  
**Stage:** 200.2  
**Status:** COMPLETE  

---

## 1. Address Space Architecture & Contracts

WinIOS defines four distinct address spaces across its virtualization hierarchy. To guarantee safety and prevent memory confusion, strict contracts govern address conversion:

| Address Space | Abbr. | Scope & Producer | Consumer / Translating Entity | Translation Rule |
|---|---|---|---|---|
| **Guest Virtual Address** | `VA` | EL0 (User) / EL1 (Kernel) instructions | Stage-1 MMU (`mmu::MMU`) | Translates to `IPA` via TTBR0/TTBR1 & TCR_EL1 |
| **Intermediate Physical Address** | `IPA` | Stage-1 output / Guest Physical Memory | Stage-2 MMU (`Stage2TranslationEngine`) | Translates to `PA` via VTTBR_EL2 & VTCR_EL2 |
| **Physical Address** | `PA` | Stage-2 output / Direct MMIO & RAM | Host backing memory (`winios::Memory`) | Direct flat index into physical RAM array |
| **Input-Output Virtual Address** | `IOVA` | Virtual DMA descriptors / Peripherals | Virtual IOMMU (`VirtualIommu`) | Translates to `PA` via Domain Page Table |

### 1.1 Non-Overlapping Invariant
- **Rule 1.1.1 (No Direct IOVA to RAM):** A virtual peripheral must never submit an `IOVA` directly to `Memory::raw_data()` or `Memory::read_*()`. All DMA accesses must pass through `VirtualIommu::translate()` to yield a valid `PA`.
- **Rule 1.1.2 (Strict Permission Intersection):** For any two-stage memory access, the effective permission is the logical conjunction of both stages:
  $$\text{EffectiveAccess} = \text{Stage1Access} \land \text{Stage2Access}$$
- **Rule 1.1.3 (Stage-1 Table Walk In IPA Space):** When Stage-2 is active (`HCR_EL2.VM == 1`), table pointers in `TTBR0_EL1` / `TTBR1_EL1` and all Level 0–3 descriptor pointers are treated as IPAs and translated through Stage-2 before memory reads.

---

## 2. Execution, Exception & Interrupt Contracts

### 2.1 Exception Level Transitions
- `EL0 -> EL1`: Initiated by `SVC #imm` or synchronous data/instruction aborts. Registers saved to `ELR_EL1` and `SPSR_EL1`.
- `EL1 -> EL2`: Initiated by `HVC #imm`, Stage-2 translation faults, or hypervisor trap flags. Registers saved to `ELR_EL2`, `SPSR_EL2`, syndrome in `ESR_EL2`, fault address in `FAR_EL2`, and fault IPA in `HPFAR_EL2`.
- `ERET`: Atomically restores `PC` from `ELR_ELx` and `PSTATE` from `SPSR_ELx`. Illegal target EL raises an Undefined Instruction exception.

### 2.2 Pointer Authentication (PAC) Contract
- Any attempt to read or write PAC key system registers (`APIAKeyLo_EL1` .. `APGAKeyHi_EL1`) at `EL0` triggers an immediate synchronous Undefined Instruction trap (`EC = 0x00`).
- Pointer authentication failures in `TrapException` mode raise synchronous exceptions with `EC = 0x1C` (`kPacTrap`) and `ISS = 0x00` (IA/IB) or `0x01` (DA/DB).
- In `CorruptUpperBits` mode, failure flips bit 62 (`kPacErrorBit`), leaving execution uninterrupted until the corrupted pointer is dereferenced.

### 2.3 Interrupt Delivery & GICv3 Routing
- DMA queue completions assert GICv3 Shared Peripheral Interrupts (`SPI 48` or `SPI 50`).
- The GIC distributor evaluates `GICD_IROUTER<n>` to deliver the interrupt exclusively to the target CPU core whose affinity matches.
- CPU cores with `ICC_PMR_EL1` set lower than the interrupt priority will mask the interrupt until priority increases or mask clears.

### 2.4 SMP Exclusive Monitor Contract
- A CPU core executing `LDXR` establishes an exclusive reservation tag in `SMPCluster` for that physical address.
- Any DMA write to an overlapping address range must call `SMPCluster::invalidate_all_monitors_for_address(pa, len)`.
- A subsequent `STXR` by the CPU core will return status `1` (failure) and discard the write.

---

## 3. Platform Boot & Snapshot Ownership Contracts

### 3.1 Platform Boot Contract (`SyntheticKernelBootInfo`)
- Memory size must be a multiple of the 16KB page granule.
- DeviceTree binary must be staged at a 16KB-aligned physical address.
- Initial CPU state on entry:
  - `X0`: Physical address of DeviceTree.
  - `PC`: Kernel entry point.
  - `PSTATE.EL = 1` (EL1 entry), SP_EL1 selected.
  - All interrupts masked (`DAIF = 0xF`).

### 3.2 Snapshot Serialization Contract
- **Atomic Independence:** Every serialized subsystem (`CPU`, `Memory`, `Stage2TranslationEngine`, `VirtualIommu`, `Arm64ePointerAuth`) produces self-contained binary blocks with explicit magic numbers, version fields, and payload lengths.
- **Transactional Restoration:** A failed deserialization in any component must not leave the live virtual platform in an inconsistent state.
