# Failure Injection Results
## Adversarial Testing and Anomaly Rejection Across 8-Core Topology Boundaries

---

## 1. Test Suite Overview

Test Group 14 in `smp_topology_scalability_tests` evaluates system resilience against malformed topologies, corrupted streams, and protocol violations.

---

## 2. Injected Anomalies & Observations

| Injection Scenario | Description | Detection Mechanism | Outcome | Result |
|---|---|---|---|---|
| **Invalid Core Count** | Requesting 3, 5, 6, 7 cores | `SmpTopology::validate()` power-of-two check | Returns `InvalidCoreCount` | **PASS** |
| **Exceeds Max Cores** | Requesting 16 or 32 cores | `core_count > MAX_CORES` bounds check | Returns `ExceedsMaxCores` | **PASS** |
| **Invalid Quantum** | Quantum set to 0 or 2,000,000 | `quantum < 1 \|\| quantum > 1,000,000` | Returns `InvalidQuantum` | **PASS** |
| **Out-of-Bounds Core ID** | Event record containing core_id = 8 | Stream decoder core ID validation | Rejects event with `InvalidCoreId` | **PASS** |
| **Corrupted Snapshot Header** | Corrupted CRC32C in snapshot | `SmpSnapshotRestorer` checksum validation | Rejects file, prevents state load | **PASS** |
| **Sequence Desynchronization** | Non-monotonic global sequence | Monotonicity validator | Flags divergence immediately | **PASS** |
| **Uncaptured DMA Injection** | `allow_uncaptured_dma = true` | Capability gate rejection | Rejects with `DmaReplayUnsupported` | **PASS** |
| **8-Core Replay Attempt** | Requesting replay for 8 cores | Replay coordinator capability gate | Returns `UnsupportedConfiguration` | **PASS** |

---

## 3. Verdict

All 8 failure injection scenarios were detected deterministically with 100% rejection accuracy.
