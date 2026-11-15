# WinIOS Campaign 200: Architectural Conformance Report

**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Architecture:** ARMv8-A / ARMv8.1-A / ARM64e Pointer Authentication  
**Host Platform:** Windows 11 x64 (MSVC C++20, `/W4 /WX /permissive-`)  
**Secondary Toolchain:** Zig 0.17.0 (`-target aarch64-macos -mcpu=apple_a16`)  
**Campaign Milestone:** Campaign 200 Verification and Conformance Closeout  

---

## 1. Executive Summary

This report establishes the technical conformance baseline for the WinIOS virtualization architecture following the rigorous completion of Campaign 200 (Stages 200.0 through 200.24). WinIOS implements an emulated, architectural execution environment modeling the 64-bit ARMv8-A / ARMv8.1-A / ARM64e processor architecture and A16 Bionic platform topology, adhering strictly to documented ARM architecture reference manuals (ARM DDI 0487) and platform contracts.

All claims in this report are grounded in deterministically reproducible unit tests and real Mach-O binary execution under MSVC C++20.

---

## 2. Memory Management Unit (MMU) Conformance

### 2.1 Stage-1 Translation Subsystem
- **Translation Granule:** Conforms to Apple A16 Bionic standard 16KB translation granule (`TG0 = 0b10` in `TCR_EL1`).
- **Addressing Limits:** Supports 48-bit Virtual Address (`VA`) space (`T0SZ = 16`) and 48-bit Physical Address (`PA`) space.
- **Hierarchical Table Walk:** Fully implements Level 0, Level 1, Level 2, and Level 3 descriptor decoding, distinguishing table descriptors (`type == 0b11`) and page/block descriptors (`type == 0b01` or `0b11`).
- **Memory Permissions (`AP[2:1]`):**
  - `AP[2:1] = 00`: Read/Write EL1-only. EL0 accesses trigger Permission Fault.
  - `AP[2:1] = 01`: Read/Write at EL0 and EL1.
  - `AP[2:1] = 10`: Read-Only EL1-only. EL0 accesses and any Write trigger Permission Fault.
  - `AP[2:1] = 11`: Read-Only at EL0 and EL1. Write accesses at any EL trigger Permission Fault.
- **Execution Controls:** UXN (Unprivileged Execute-Never) and PXN (Privileged Execute-Never) bit enforcement.
- **Access Flag (AF):** Hardware descriptor bit 10 tracking; generates Access Flag Fault (`DFSC = 0b001000`–`0b001011`) when unasserted if hardware AF management is disabled.

### 2.2 Stage-2 Translation Subsystem
- **Hypervisor Control:** Controlled via `VTCR_EL2` and `VTTBR_EL2` containing the 16-bit Virtual Machine Identifier (`VMID`).
- **Intermediate Physical Address (IPA) Translation:** Translates IPAs output from Stage-1 to final System Physical Addresses (PA).
- **Stage-2 Permissions (`S2AP`):** Enforces independent stage-2 read and write permissions (None, Read-Only, Write-Only, Read/Write).
- **Stage-2 Execute-Never (`XN`):** Restricts instruction fetches across VM boundaries.

### 2.3 Combined Translation and Fault Priority
- **Resolution Pipeline:** Complete virtual address translation resolves $VA \to IPA$ via Stage-1, followed by $IPA \to PA$ via Stage-2.
- **Fault Hierarchy:** Conforms to ARM DDI 0487 priority rules:
  1. Stage-1 Translation / Address Size Faults.
  2. Stage-1 Permission / Access Flag Faults.
  3. Stage-2 Translation / Address Size Faults (reported with Stage-2 syndrome and IPA).
  4. Stage-2 Permission Faults.
- **Permission Intersection:** Final access rights represent the strictest intersection of Stage-1 and Stage-2 permissions:
  $$\text{EffectiveAccess} = \text{Access}_{S1} \cap \text{Access}_{S2}$$

---

## 3. Translation Lookaside Buffer (TLB) & Cache Architecture

### 3.1 Tagged TLB Caching & Isolation
- **Tagging Dimensions:** Entries are tagged by Virtual Address, Address Space Identifier (ASID, 16-bit), and Virtual Machine Identifier (VMID, 16-bit).
- **Hit Criteria:** A TLB lookup requires matching $(VA \gg 14)$ along with $(ASID \lor \text{Global})$ and $VMID$.
- **Invalidation Instructions:**
  - `TLBI VAE1`: Invalidates entries matching specific VA and current ASID.
  - `TLBI IPAS2E1`: Invalidates Stage-2 IPA entries for current VMID.
  - `TLBI VMALLE1`: Invalidates all Stage-1 entries for current VMID.
- **SMP Multicore Broadcast:** TLB invalidation commands issued by any CPU core broadcast synchronously across all 8 cores in the `SMPCluster`.
- **Thread Safety:** Protected by thread-safe synchronization to guarantee concurrent core lookups and updates without race conditions.

### 3.2 Memory Types & Attributes
- **MAIR_EL1 Decoding:** Memory attribute registers decode into architectural memory classes:
  - `Device-nGnRnE` (Non-gathering, non-reordering, non-early-write-acknowledgment).
  - `Device-nGnRE`.
  - `Normal Memory` (Inner/Outer Write-Back, Read/Write Allocate, Cacheable).
- **Block Cache Operations:** Implements `DC ZVA` (Zero Virtual Address) operating on 64-byte blocks matching the A16 Bionic L1/L2 cache line size.
- **Exclusive Monitor Reservation Granule:** Set to 64 bytes (`ERG = 4`), guaranteeing correct multi-core `LDXR`/`STXR` and `CAS` atomicity.

---

## 4. Virtual IOMMU & DMA Subsystem Conformance

### 4.1 Device Table & Domain Isolation
- **Device Identification:** 16-bit Device ID addressing peripheral endpoints.
- **IOMMU Domains:** Independent page table domains mapping 64-bit IO Virtual Addresses (`IOVA`) to physical addresses (`PA`).
- **Domain Sharing:** Multiple physical or virtual devices can attach to a single shared translation domain or remain strictly isolated.

### 4.2 Translation & Fault Handling
- **Translation Modes:** Supports bypass (direct PA) and translated (`IOVA \to PA`).
- **Fault Generation:** Real-time detection of:
  - Unmapped IOVA Fault.
  - Read-Only Permission Fault (DMA write to RO page).
  - Out-of-Bounds Address Fault.
- **Engine Protection:** Faults cleanly halt the offending DMA descriptor and record fault status without host or guest system crashes.

### 4.3 Coherency & Completion
- **Cache Coherency:** Direct memory writes via DMA engine invalidate overlapping exclusive reservation monitors across all 8 CPU cores.
- **Interrupt Delivery:** DMA descriptor completion with `kDmaFlagInterruptOnComplete` automatically asserts GICv3 Shared Peripheral Interrupt (SPI 48).

---

## 5. ARM64e Pointer Authentication (PAC) Conformance

### 5.1 Cryptographic Primitives & Instruction Coverage
- **Algorithm:** Bounded research implementation of QARMA/QARMA64 algorithm mapping 64-bit pointer and 64-bit modifier to a cryptographic PAC field.
- **Instruction Support:** Full architectural emulation of:
  - Pointer Signing: `PACIA`, `PACIB`, `PACDA`, `PACDB`, `PACIZA`, `PACIZB`, `PACDZA`, `PACDZB`.
  - Pointer Authentication: `AUTIA`, `AUTIB`, `AUTDA`, `AUTDB`, `AUTIZA`, `AUTIZB`, `AUTDZA`, `AUTDZB`.
  - Pointer Stripping: `XPACI`, `XPACD`.
  - Generic Authentication: `PACGA`.
  - Branch-and-Authenticate: `BRAA`, `BRAB`, `BLRAA`, `BLRAB`, `RETAA`, `RETAB`.

### 5.2 Privilege Boundaries & Exception Handling
- **Key Register Isolation:** Key registers (`APIAKey_EL1`, `APIBKey_EL1`, `APDAKey_EL1`, `APDBKey_EL1`, `APGAKey_EL1`) enforce `min_el == 1`. Accesses from EL0 trigger an Undefined Instruction Exception (`EC = 0x00`).
- **Process Isolation:** PAC keyrings are strictly isolated across guest thread control blocks (TCBs); context switching swaps keyrings deterministically.
- **Failure Modes:**
  - `TrapException`: Generates synchronous exception with `EC = 0x1C` (Pointer Authentication failure).
  - `CorruptUpperBits`: Bit 62 flipped to render authenticated pointer illegal for subsequent dereference.

---

## 6. Interrupt Architecture (GICv3) & Privilege Levels

### 6.1 GICv3 Topology & Interface
- **Memory-Mapped Control:** Conforms to GICv3 distributor (`GICD`, base `0x08000000`) and redistributor (`GICR`, base `0x080A0000`) memory ranges.
- **Interrupt Routing:**
  - SGIs (0–15): Inter-processor signaling.
  - PPIs (16–31): Per-core private interrupts (e.g., GICv3 architectural virtual timer).
  - SPIs (32–1019): Shared peripheral interrupts routeable to specific CPU targets via affinity registers (`GICD_IROUTER`).
- **Priority Masking:** `ICC_PMR_EL1` priority filtering drops or masks lower-priority interrupts.
- **Interrupt Lifecycle:** Full state machine transition: Inactive $\to$ Pending $\to$ Active (via `ICC_IAR1_EL1` read) $\to$ Inactive (via `ICC_EOIR1_EL1` write).

### 6.2 Privilege Levels & Transitions
- **EL0 $\leftrightarrow$ EL1:** Full transition support. `SVC #imm` raises synchronous exception (`EC = 0x15`), saving PC to `ELR_EL1` and PSTATE to `SPSR_EL1`.
- **ERET Validation:** Restores execution state. Enforces `PSTATE.IL` detection: illegal transitions generate synchronous exception (`EC = 0x0E`).

---

## 7. State Preservation, Snapshot & Replay

- **Complete Serialization:** Multi-core CPU state, MMU TLBs, Virtual IOMMU domains, GICv3 interrupt states, and guest physical memory serialize into versioned binary snapshot containers.
- **Deterministic Replay:** Replaying execution from a mid-flight snapshot yields 100% bit-exact register state, memory contents, and instruction retirement counts.

---

## 8. Genuine Guest Execution

- **Real Mach-O Execution:** Validated execution of genuine ARM64e binary (`guest_stage200_end_to_end_workload`) compiled via Zig 0.17.0 for `aarch64-macos -mcpu=apple_a16`.
- **Execution Verification:** Executes genuine PAC signing, authentication, generic tag calculation, and memory stores, retiring with signature token `0x2000C0DE`.
