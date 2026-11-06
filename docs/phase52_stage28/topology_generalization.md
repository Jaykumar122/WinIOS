# Topology Generalization Specification
## Multi-Core Topology Validation, Bounds Checking & Power-of-Two Enforcement

---

## 1. Overview

Stage 28 introduces the formal `SmpTopology` class (`src/cpu/smp_topology.h` and `src/cpu/smp_topology.cpp`) to govern SMP configuration constraints across WinIOS.

---

## 2. Configuration Parameters (`SmpTopologyConfig`)

```cpp
struct SmpTopologyConfig {
    uint32_t core_count = 1;                         // Must be power-of-two (1, 2, 4, 8)
    uint64_t instruction_quantum = 1000;             // Must be between 1 and 1,000,000
    uint64_t memory_size_bytes = 64 * 1024 * 1024;   // Shared RAM size in bytes
    bool allow_replay_eight_core = false;            // MUST remain false in Stage 28
    bool allow_uncaptured_dma = false;               // MUST remain false (BLK-SMP-03 guard)
};
```

---

## 3. Validation Rules

1. **Power-of-Two Core Counts**:
   - `core_count` must satisfy `(core_count & (core_count - 1)) == 0`.
   - Permitted topologies: 1, 2, 4, 8.
   - Core counts of 0, 3, 5, 6, 7 are rejected with `InvalidCoreCount`.

2. **Core Upper Limit**:
   - `core_count` must not exceed `MAX_CORES` (`8`).
   - Topologies of 16, 32, 64 are rejected with `ExceedsMaxCores`.

3. **Quantum Range**:
   - `instruction_quantum` must be `>= 1` and `<= 1,000,000`.
   - Zero or out-of-range values return `InvalidQuantum`.

4. **Memory Allocation**:
   - `memory_size_bytes` must be non-zero and aligned to 4KB page boundaries.

5. **Replay Capability Enforcements**:
   - Core counts `> 4` are strictly rejected for replay in Stage 28 with `ReplayUnsupported`.
   - `allow_uncaptured_dma == true` is strictly rejected with `DmaReplayUnsupported`.
