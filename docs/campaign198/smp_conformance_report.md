# WinIOS Campaign 198: 8-Core Shared-Memory Conformance Report

**Target Research Architecture:** ARMv8.1-A / ARM64e (iPhone 15 / Apple A16 Bionic `T8120`)  
**Granule Profile:** 16 KB Page Translation Granule | 64-Byte Cache Line / Exclusive Reservation Granule  
**Cluster Architecture:** 8-Core Shared Physical Memory (`SMPCluster`)  
**Classification:** `GUEST_MEMORY_MODEL` / `SYNTHETIC_KERNEL`  

---

## 1. Executive Summary

Campaign 198 Stage 11 establishes full 8-core shared-memory architectural conformance for WinIOS. The simulated platform models an 8-core ARM64 cluster where all cores access a unified physical memory address space through coherent interconnect semantics, enforce multi-copy atomicity, maintain independent per-core MMU/TLB states, and participate in cluster-wide broadcast invalidation (TLBI and exclusive monitor snooping).

---

## 2. Shared Physical Memory Coherency

In WinIOS, the 8 CPU cores execute within a shared physical memory space provided by `winios::Memory` and `winios::SmpSharedMemory`:
- **Physical Address Space Identity:** Any physical address written by Core $i$ is immediately observable by Cores $j \neq i$ following the completion of the store in the unified physical backing store.
- **Striped Concurrent Synchronization:** To prevent data races between concurrent host threads simulating CPU cores, the memory subsystem utilizes 64 striped recursive read/write locks, hashing addresses into 64-byte aligned buckets.

---

## 3. 8-Way Exclusive Monitor Architecture

ARMv8-A specifies that each core possesses a local exclusive monitor and the system implements an open/exclusive reservation architecture:
- **Reservation Granule:** 64 bytes (`CTR_EL0.ERG = 4`).
- **Targeted Snooping & Invalidation:**
  - When Core $i$ executes `LDXR`/`LDAXR`, its local monitor records the reservation tag for physical address $PA$ aligned to 64 bytes.
  - When Core $j$ ($j \neq i$) performs an ordinary or exclusive store overlapping that 64-byte granule, `SMPCluster::invalidate_exclusive_reservations` clears the reservation on Core $i$.
  - Disjoint 64-byte reservations on other cores remain valid, ensuring non-interfering parallel lock-free operations across all 8 cores.

---

## 4. Cross-Core Translation & TLB Broadcast Invalidation

The 8-core virtual memory architecture maintains independent per-core translation state:
- Each core possesses an independent `MMU` instance with its own `TTBR0_EL1`, `TTBR1_EL1`, `TCR_EL1`, `MAIR_EL1`, and TLB cache.
- **Cluster Invalidation Broadcasts:**
  - `TLBI ALLE1IS`: Invalidates all EL1/EL0 TLB entries across all 8 cores.
  - `TLBI VAE1IS`: Broadcasts virtual address and ASID invalidation across all 8 cores.
  - `TLBI ASIDE1IS`: Flushes translations matching a specific ASID across all 8 cores.
  - `TLBI VAAE1IS`: Flushes translations matching all ASIDs for the given VA.
- Cross-core broadcasts ensure that operating system kernel page-table modifications (such as page migration, unmapping, or permission revocation) are immediately reflected across all cores.

---

## 5. PSCI Multicore Lifecycle & Verification

The synthetic cluster integrates ARM Power State Coordination Interface (PSCI v1.0+):
- Core 0 acts as the Boot CPU (Primary Core).
- Cores 1..7 remain in a quiescent, halted state (`WFI`/`WFE` simulation) until activated via PSCI `CPU_ON` (Function ID `0xC4000003` for SMC64).
- Upon receiving `CPU_ON`, the target core's Program Counter (`PC`) and Context ID (`X1`) are set, its execution status is switched to runnable, and it begins executing its startup routine.

---

## 6. Verification Results

All 4 test phases of `stage198_11_eight_core_memory_tests` pass with zero failures:
1. `test_eight_core_instantiation_and_shared_memory`: 100% memory coherency across 8 cores.
2. `test_eight_core_exclusive_reservations_and_invalidation`: 8-way exclusive reservations with exact 64-byte granule invalidation targeting.
3. `test_eight_core_psci_boot`: Successful secondary core lifecycle bring-up from Core 0 across Cores 1..7.
4. `test_eight_core_cluster_tlbi_broadcast`: Successful broadcast invalidation of TLB entries across 8 cores.
