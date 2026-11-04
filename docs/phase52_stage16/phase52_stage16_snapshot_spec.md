# WinIOS Phase 52, Stage 16: Architectural Snapshot (.wsnap) Specification

**Document Identifier:** `WINIOS-SPEC-52-16-002`  
**Status:** Approved Technical Specification  
**Date:** 2026-11-04  
**Author:** WinIOS System Architecture Team  
**Classification:** Core System Architecture Specification  

---

## 1. Overview and Design Objectives

The WinIOS Snapshot Specification defines the format, layout, and restoration procedures for `.wsnap` files. A `.wsnap` file represents an exact, bit-level, self-contained capture of all simulated architectural and physical state at an unambiguous instruction retirement boundary.

### 1.1 Design Principles
1. **Architectural Completeness:** Every piece of mutable state in the simulated environment—including CPU general-purpose registers, banked stack pointers, system registers across EL0–EL3, SIMD/FP registers, MMU state, debug units, PMU counters, GICv3 distributor/redistributor/CPU interfaces, timers, and guest physical memory—must be preserved.
2. **Determinism on Restore:** Restoring a `.wsnap` file into a clean execution harness must produce an emulator instance whose subsequent single-step execution is indistinguishable from the original system running from that instant forward.
3. **Chunked Sparse Memory Layout:** Guest physical memory is organized into fixed-size, relocatable chunks with zero-page elimination and optional compression (LZ4/Zstd) to avoid gigabyte-scale uncompressed disk bloat.
4. **Data Integrity:** Every section and overall archive is protected by CRC32C and SHA-256 checksums to detect truncation, silent corruption, or version incompatibilities before restoration begins.

---

## 2. File Format and Header Structure

A `.wsnap` snapshot file consists of:
1. Fixed 64-byte File Header.
2. Section Directory Table (Array of section headers).
3. Payload Sections:
   - Section 1: Memory Layout & Sparse Page Chunks
   - Section 2: GICv3 Global Distributor State
   - Section 3: VirtualTime Global State
   - Section 4: PSCI Global Affinity and Lifecycle State
   - Section 5..N: Per-Core Architectural CPU Snapshots (Core 0 to Core N-1)
4. Archive Trailer (SHA-256 digest over entire file content prior to trailer).

### 2.1 Fixed File Header (64 bytes)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Magic: 'W' 'S' 'N' 'A' 'P' '\0' '\1' '\0'               |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Version              |          Core Count           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      Compression Type         |           Flags               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Creation Timestamp                      |
|                            (uint64)                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Guest Memory Capacity (Bytes)               |
|                            (uint64)                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Section Directory File Offset               |
|                            (uint64)                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Section Count (uint32)    |    Reserved   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Header CRC32C (uint32)     |    Reserved   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

```cpp
struct WSnapHeader {
    uint8_t  magic[8];             // 'W','S','N','A','P','\0','\1','\0'
    uint16_t version;               // File format version (1)
    uint16_t core_count;            // Number of simulated CPU cores (1..128)
    uint16_t compression_type;      // 0 = None, 1 = LZ4, 2 = Zstd
    uint16_t flags;                 // Bit 0: Sparse Memory, Bit 1: Incremental
    uint64_t timestamp_ns;          // Host timestamp when snapshot created
    uint64_t memory_capacity;       // Total guest physical memory capacity
    uint64_t section_dir_offset;    // File offset to Section Directory Table
    uint32_t section_count;         // Total sections in directory
    uint32_t reserved0;             // Alignment padding (0)
    uint32_t header_crc32c;         // CRC32C of bytes [0..55]
    uint32_t reserved1;             // Alignment padding (0)
};
static_assert(sizeof(WSnapHeader) == 64, "WSnapHeader must be exactly 64 bytes");
```

### 2.2 Section Directory Table

Each entry in the Section Directory Table describes one payload section:

```cpp
struct WSnapSectionDescriptor {
    uint32_t section_type;          // SnapshotSectionType enum
    uint32_t section_id;            // Sub-identifier (e.g., Core ID for CPU sections)
    uint64_t file_offset;           // Absolute offset in .wsnap file
    uint64_t compressed_size;       // Size on disk (bytes)
    uint64_t uncompressed_size;     // Size after decompression (bytes)
    uint32_t section_crc32c;        // CRC32C over compressed payload bytes
    uint32_t reserved;              // Alignment padding
};
static_assert(sizeof(WSnapSectionDescriptor) == 40, "Descriptor must be 40 bytes");
```

---

## 3. CPU Core Architectural State Specification

Each CPU core section stores an exact replica of `winios::CPU` internal state.

### 3.1 Binary Layout of CPU Section

```cpp
struct alignas(16) WSnapCpuState {
    // 1. Identification and Instruction Lifecycle Counters
    uint32_t core_id;
    uint32_t padding0;
    uint64_t instruction_retirement_count;
    uint64_t cycle_count;
    uint64_t mpidr_el1;

    // 2. Program Counter and General-Purpose Registers
    uint64_t pc;
    uint64_t x[31];                 // X0 - X30

    // 3. Banked Stack Pointers and Selection
    uint64_t sp_el0;
    uint64_t sp_el1;
    uint64_t sp_el2;
    uint64_t sp_el3;
    uint32_t sp_sel;                // SPSel (0 = SP_EL0, 1 = SP_ELx)
    uint32_t current_el;            // Current Exception Level (0..3)

    // 4. Processor State (PSTATE)
    uint32_t pstate_nzcv;           // Bits: N(31), Z(30), C(29), V(28)
    uint32_t pstate_daif;           // Bits: D(9), A(8), I(7), F(6)
    uint32_t pstate_flags;          // Bit 0: PAN, Bit 1: UAO, Bit 2: BTYPE, Bit 3: SS, Bit 4: IL
    uint32_t padding1;

    // 5. SIMD / Floating-Point Registers (128 bits = 16 bytes each)
    struct {
        uint64_t low;
        uint64_t high;
    } v[32];                        // V0 - V31
    uint32_t fpcr;
    uint32_t fpsr;

    // 6. Execution Control and Core Lifecycle
    uint8_t  is_halted;
    uint8_t  waiting_for_interrupt;
    uint8_t  waiting_for_event;
    uint8_t  event_register;
    uint32_t reserved_lifecycle;

    // 7. System Registers - EL1
    uint64_t sctlr_el1;
    uint64_t tcr_el1;
    uint64_t ttbr0_el1;
    uint64_t ttbr1_el1;
    uint64_t mair_el1;
    uint64_t vbar_el1;
    uint64_t elr_el1;
    uint64_t spsr_el1;
    uint64_t esr_el1;
    uint64_t far_el1;
    uint64_t cntkctl_el1;

    // 8. System Registers - EL2 (Hypervisor & Stage 2 Translation)
    uint64_t hcr_el2;
    uint64_t vbar_el2;
    uint64_t elr_el2;
    uint64_t spsr_el2;
    uint64_t esr_el2;
    uint64_t far_el2;
    uint64_t hpfar_el2;
    uint64_t vttbr_el2;
    uint64_t vtcr_el2;
    uint64_t cntvoff_el2;
    uint64_t cnthctl_el2;
    uint64_t mdcr_el2;

    // 9. System Registers - EL3 (Secure Monitor)
    uint64_t scr_el3;
    uint64_t cptr_el3;
    uint64_t mdcr_el3;
    uint64_t vbar_el3;
    uint64_t elr_el3;
    uint64_t spsr_el3;
    uint64_t esr_el3;
    uint64_t far_el3;

    // 10. Architectural Generic Timers (Core-Local)
    struct {
        uint32_t enable;
        uint32_t imask;
        uint32_t istatus;
        uint32_t padding;
        uint64_t cval;
    } phys_timer, virt_timer, cnthp_timer;
    uint64_t cntfrq_el0;

    // 11. Debug Architecture Registers
    uint64_t mdscr_el1;
    uint64_t bvr[16];
    uint32_t bcr[16];
    uint64_t wvr[16];
    uint32_t wcr[16];

    // 12. PMU Architectural Registers
    uint32_t pmcr;
    uint32_t pmcntenset;
    uint32_t pmovsset;
    uint32_t pmuserenr;
    uint32_t pmintenset;
    uint32_t pmccfiltr;
    uint64_t pmccntr;
    uint32_t pmevtyper[6];
    uint32_t pmevcntr[6];

    // 13. GICv3 CPU Interface Registers
    uint32_t icc_pmr_el1;
    uint32_t icc_bpr0_el1;
    uint32_t icc_bpr1_el1;
    uint32_t icc_ctlr_el1;
    uint32_t icc_sre_el1;
    uint32_t icc_igrpen0_el1;
    uint32_t icc_igrpen1_el1;
    uint32_t icc_rpr_el1;
    uint32_t icc_hppir0_el1;
    uint32_t icc_hppir1_el1;
    uint32_t icc_iar0_el1;
    uint32_t icc_iar1_el1;
    uint32_t icc_eoir0_el1;
    uint32_t icc_eoir1_el1;
    uint32_t icc_dir_el1;
    uint32_t reserved_gic;
};
```

---

## 4. Memory Snapshot Specification

Guest physical memory in WinIOS is managed through `winios::Memory`. Rather than dumping an uncompressed flat buffer (e.g. 4GB = 4GB disk file), the snapshot implements a chunked sparse page mapping.

### 4.1 Chunk Organization
- **Chunk Size:** 64 KB (16 standard 4 KB AArch64 pages).
- **Zero-Page Optimization:** Chunks that contain only zero bytes are omitted from the snapshot entirely. The section header contains a total chunk count, followed by an index table of allocated chunks.

```cpp
struct WSnapMemorySectionHeader {
    uint64_t base_physical_address; // Starting physical address (typically 0x0)
    uint64_t total_memory_bytes;    // Total allocated guest RAM size
    uint32_t chunk_size_bytes;      // Default: 65,536 (64 KB)
    uint32_t total_chunks;          // Total chunks in range
    uint32_t non_zero_chunks;       // Number of populated chunks stored
    uint32_t flags;                 // Bit 0: Compressed
};

struct WSnapMemoryChunkDescriptor {
    uint32_t chunk_index;           // Chunk index (paddr = base + index * chunk_size)
    uint32_t compressed_bytes;      // Compressed size on disk
    uint32_t raw_crc32c;            // CRC32C of uncompressed 64KB data
    uint32_t flags;                 // Compression algorithm (LZ4/Zstd)
};
```

### 4.2 MMU State and TLB Invalidation
- **TLB Persistence Policy:** TLB state represents transient cached translations. During snapshot restoration, all TLBs across all cores are explicitly invalidated (`TLBI ALLE1`, `TLBI ALLE2`, `TLBI ALLE3`). 
- **Determinism Justification:** In AArch64 architecture, page table changes require explicit software invalidation (`TLBI`). Because translations are completely deterministic functions of page table memory and system registers (`SCTLR_ELx`, `TCR_ELx`, `TTBR0/1_ELx`), invalidating the cache and allowing the MMU to perform deterministic architectural page walks on demand guarantees identical execution without the complexity of serializing implementation-specific TLB tags.

---

## 5. Device and Peripheral State Specification

### 5.1 GICv3 Distributor and Redistributors
The GICv3 model in WinIOS (`winios::GICv3`) consists of global distributor state and per-core redistributor state.

```cpp
struct WSnapGicv3DistributorState {
    uint32_t gicd_ctlr;
    uint32_t gicd_typer;
    uint32_t gicd_iidr;
    uint32_t padding;
    
    // SPI Configuration and Status (INTID 32..1019)
    uint32_t isenabler[32];         // Interrupt Set-Enable Registers
    uint32_t ispendr[32];           // Interrupt Set-Pending Registers
    uint32_t isactiver[32];         // Interrupt Set-Active Registers
    uint8_t  ipriorityr[1024];      // Priority per interrupt ID
    uint8_t  itargetsr[1024];       // Target CPU affinity per SPI
    uint32_t igroupr[32];           // Interrupt Group (Group 0 vs Group 1)
};

struct WSnapGicv3RedistributorState {
    uint32_t core_id;
    uint32_t rd_ctlr;
    uint32_t rd_status;
    uint32_t padding;
    
    // SGIs and PPIs (INTID 0..31)
    uint32_t sgi_isenabler;
    uint32_t sgi_ispendr;
    uint32_t sgi_isactiver;
    uint32_t sgi_igroupr;
    uint8_t  sgi_ipriorityr[32];
};
```

### 5.2 VirtualTime Model State
`winios::VirtualTime` coordinates physical and virtual counter advancement.

```cpp
struct WSnapVirtualTimeState {
    uint64_t base_physical_ticks;   // Base physical counter tick
    uint64_t base_virtual_ticks;    // Base virtual counter tick
    uint32_t counter_frequency_hz;  // Clock frequency (e.g. 62.5 MHz)
    uint32_t time_scaling_factor;   // Scaler for simulated instruction pacing
};
```

### 5.3 PSCI State
`winios::PSCI` tracks multi-core power management states:

```cpp
struct WSnapPsciCoreState {
    uint32_t core_id;
    uint32_t power_state;           // 0 = ON, 1 = OFF, 2 = SUSPENDED
    uint64_t entry_point_address;   // Boot entry point set by CPU_ON
    uint64_t context_id;            // Context parameter passed on wakeup
};
```

---

## 6. Snapshot Restoration Sequencing and Validation Rules

To prevent undefined intermediate states, restoration must follow a strict sequential ordering:

```
+--------------------------------------------------------------------------------+
|                        Snapshot Restoration Sequence                           |
|                                                                                |
| 1. Header Validation:                                                          |
|    - Verify Magic ('WSNAP\0\1\0')                                              |
|    - Check Version compatibility                                               |
|    - Verify Header CRC32C                                                      |
|                                                                                |
| 2. Memory Reconstitution:                                                      |
|    - Allocate guest RAM to match Header.memory_capacity                        |
|    - Zero-fill all pages                                                       |
|    - Decompress and write non-zero sparse chunks to guest physical addresses   |
|    - Validate each chunk CRC32C                                                |
|                                                                                |
| 3. Global Subsystem Restoration:                                               |
|    - Restore GICv3 Distributor state (SPI configurations, priorities)          |
|    - Restore VirtualTime baseline ticks and frequency                          |
|    - Restore PSCI core states and affinity mappings                            |
|                                                                                |
| 4. Per-Core Initialization (Cores 0 to N-1):                                   |
|    - Populate GPRs (X0..X30, PC, SP_EL0..3, SPSel)                            |
|    - Populate PSTATE (NZCV, DAIF, CurrentEL, PAN, UAO)                         |
|    - Populate SIMD registers (V0..V31, FPCR, FPSR)                             |
|    - Populate System Registers (EL1, EL2, EL3)                                 |
|    - Populate Debug and PMU registers                                          |
|    - Restore GICv3 CPU Interface & Redistributor state                         |
|    - Restore Local Timers (phys, virt, cnthp) and reset timer alarm callbacks  |
|                                                                                |
| 5. Post-Restore Invalidation & Safety Checks:                                  |
|    - Flush all core TLB entries (force cold architectural page walks)          |
|    - Clear transient execution flags (pipeline bubbles, uncommitted branches)  |
|    - Synchronize core instruction retirement counter to recorded snapshot value|
|    - Compute baseline verification hash across full system state               |
+--------------------------------------------------------------------------------+
```

### 6.1 Validation Invariants
- If any section CRC32C fails, the restore operation aborts immediately and leaves the execution engine unchanged.
- The destination `core_count` in the execution harness must be greater than or equal to `WSnapHeader.core_count`.
- All restored `PC` addresses must be 4-byte aligned (AArch64 requirement). Any unaligned PC produces a fatal validation error.
