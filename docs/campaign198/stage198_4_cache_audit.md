# WinIOS Campaign 198: Stage 198.4 Cache Architecture & Audit Report

**Date:** 2026-11-15  
**Campaign:** 198 — ARM64 Memory-System Integration, Cache Coherency, DMA Consistency & SMP Virtual Platform Conformance  
**Target Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Line Granule:** 64 bytes (`DminLine = 4`, `IminLine = 4`, `DCZID_EL0.BS = 4`)  

---

## 1. Architectural Cache Geometry and System Registers

In ARMv8-A / ARM64e, cache organization, block sizes, and EL0 access privileges are governed by architectural system registers:

1. **`CTR_EL0` (Cache Type Register):**
   - Architectural Value: `0x00004004`
   - `DminLine[19:16] = 4`: Log2 of words in smallest data cache line ($2^4 \times 4 = 64$ bytes).
   - `IminLine[3:0] = 4`: Log2 of words in smallest instruction cache line ($2^4 \times 4 = 64$ bytes).
   - `L1Ip[15:14] = 0b00`: VIPT (Virtually Indexed, Physically Tagged) L1 instruction cache.

2. **`DCZID_EL0` (Data Cache Zero ID Register):**
   - Architectural Value: `0x00000004`
   - `BS[3:0] = 4`: Log2 of words in DC ZVA zero block ($2^4 \times 4 = 64$ bytes).
   - `DZP[4] = 0`: DC ZVA is permitted and available.

3. **`SCTLR_EL1` Access Controls:**
   - `DZE` (bit 14): Controls `DC ZVA` execution at EL0. If 0, `DC ZVA` from EL0 generates an Undefined Instruction exception (EC 0x00). If 1, `DC ZVA` is permitted at EL0.
   - `UCI` (bit 26): Controls EL0 execution of cache maintenance instructions (`DC CVAC`, `DC CVAU`, `DC CIVAC`, `DC IVAC`, `IC IVAU`). If 0, EL0 execution generates an Undefined Instruction exception. If 1, permitted.
   - `IC IALLU` and `IC IALLUIS`: Always restricted to EL1+. Execution from EL0 unconditionally generates an Undefined Instruction exception regardless of `SCTLR_EL1.UCI`.

---

## 2. Coherency Domains: PoC vs. PoU

1. **Point of Coherency (PoC):**
   - The point at which all memory observers (all CPU cores, DMA masters, GPU) observe identical memory contents.
   - Instructions operating to PoC:
     - `DC CVAC`: Clean data cache to PoC.
     - `DC CIVAC`: Clean and Invalidate data cache to PoC.
     - `DC IVAC`: Invalidate data cache to PoC.

2. **Point of Unification (PoU):**
   - The point at which the instruction cache, data cache, and translation table walkers of a given core or cluster observe a consistent memory image.
   - Instructions operating to PoU:
     - `DC CVAU`: Clean data cache to PoU.
     - `IC IVAU`: Invalidate instruction cache line to PoU.
     - `IC IALLU` / `IC IALLUIS`: Invalidate all instruction caches to PoU.

---

## 3. Cache Line States and Invalidation Protocol

- Each 64-byte block tracks coherency state:
  - `Invalid`: Block does not contain valid cached data.
  - `Clean`: Block contents match physical memory.
  - `Dirty`: Block contents modified relative to physical memory.
- In WinIOS virtual platform:
  - Cache maintenance by VA translates through the Stage-1 / Stage-2 MMU.
  - Access permissions are enforced:
    - Clean (`DC CVAC`, `DC CVAU`): requires Read permission.
    - Invalidate (`DC IVAC`): requires Write permission.
    - Clean & Invalidate (`DC CIVAC`): requires Read and Write permissions.
    - Zero (`DC ZVA`): requires Write permission.
  - Device memory (`Device-nGnRnE`, `Device-nGnRE`) is uncacheable; `DC ZVA` to Device memory strictly triggers an Alignment Fault (FSC 0x21).
