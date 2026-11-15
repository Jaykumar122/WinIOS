# WinIOS Campaign 199: Architecture Decisions

**Target Architecture:** ARMv8.5-A / ARM64e (iPhone 15 / Apple A16 Bionic `T8120`)  
**Scope:** Stages 199.0 – 199.19  

---

## Decision 199-01: 16KB Stage-2 Translation Granule Default
- **Context:** Apple Silicon processors from A12 through A16 Bionic and M-series utilize a 16KB page granule for virtual and intermediate physical memory.
- **Decision:** Configure `Stage2TranslationEngine` to default to 16KB granule (`TG0 = 0b10`), Level 2 table start (`SL0 = 0b01`), and 48-bit IPA space (`T0SZ = 16`). Provide flexible configurability for 4KB and 64KB granules to support standard ARMv8 hypervisors.
- **Consequences:** Eliminates artificial 4KB mismatches when evaluating Darwin kernel paging data structures and aligns with hardware behavior.

---

## Decision 199-02: Domain-Based Virtual IOMMU Architecture (Apple DART Model)
- **Context:** On iPhone 15 hardware, DMA devices access memory through Device Address Resolution Tables (DARTs). DMA memory isolation is essential to prevent peripheral corruption of guest kernel pages.
- **Decision:** Implement `VirtualIommu` using independent `IommuDomain` contexts. Each domain owns its IOVA->PA translation table and permissions. Devices attach by 32-bit ID. Unattached devices or unmapped IOVAs fail with deterministic fault reports.
- **Consequences:** Provides robust hardware-accurate DMA translation and memory protection, allowing multiple virtual peripherals to operate in isolated address spaces.

---

## Decision 199-03: Bounded Architectural PAC Emulation
- **Context:** ARM64e pointer authentication relies on hardware cryptography (QARMA64/Apple proprietary). Emulation must be deterministic, computationally bounded, and architecturally accurate.
- **Decision:** Implement deterministic 128-bit key hashing using a bounded cryptographic mix of key halves, modifier, and pointer address. Store the signature in the upper 16 bits ([63:48]), toggle bit 62 on failure, and generate synchronous exceptions with `EC = 0x1C` (`kPacTrap`).
- **Consequences:** Enables Darwin binaries compiled with `-fptrauth` to execute correctly, sign pointers, authenticate returns (`RETAA`), and catch corrupted return addresses with exact architectural exceptions.

---

## Decision 199-04: GICv3 Multi-Core Affinity Routing
- **Context:** In an 8-core virtual platform, peripheral interrupts (such as DMA completion) must be routed to designated target cores without interrupting other cores.
- **Decision:** Implement GICv3 `GICD_IROUTER` registers at `GICD_BASE + 0x6100 + (INTID - 32) * 8` storing CPU affinity targets. When an SPI is asserted, only the matching target core's interrupt line is raised.
- **Consequences:** Enables precise interrupt targeting and prevents spurious cross-core context switches during high-throughput I/O.

---

## Decision 199-05: Recursive Mutex for IOMMU Deserialization Safety
- **Context:** `VirtualIommu::deserialize` calls `reset()` to wipe old domains before restoring saved state, creating a potential self-deadlock if non-recursive locking is used.
- **Decision:** Use `std::recursive_mutex` in `VirtualIommu` and protect all API entry points with `std::lock_guard<std::recursive_mutex>`.
- **Consequences:** Guarantees thread-safe access from concurrent DMA threads while permitting seamless in-place state deserialization.

---

## Decision 199-06: Dual Device Tree Representation
- **Context:** The virtual platform must satisfy both Apple iBoot validation rules for Darwin guest exploration and standard Linux/KVM FDT parsing.
- **Decision:** Support both formats:
  - Canonical Apple DeviceTree with 6-core (2 Everest + 4 Sawtooth) topology, PMGR, DISP0, ANS2, and DART nodes.
  - Flattened Device Tree (FDT DTB) with SMMUv3 and Hypervisor nodes.
- **Consequences:** Complete compatibility across diverse guest kernel handoff contracts without compromising architectural fidelity.
