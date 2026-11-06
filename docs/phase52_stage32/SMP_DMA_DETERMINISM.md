# WinIOS — SMP DMA Determinism Verification
## Multi-Core Determinism, Repetition & Exclusive Monitor Validation

---

## 1. Multi-Core Deterministic Reproduction

Deterministic SMP DMA replay was validated across 1-, 2-, 4-, and 8-core virtual configurations. Each configuration was executed through 10 repeated record-and-replay cycles under active DMA contention.

### Deterministic Cluster Hash Results

| Topology | Cores | Repetitions | Identical Hash | Canonical Cluster Hash | Status |
|---|---|---|---|---|---|
| 1-Core | Core 0 | 10 / 10 | 100% | `0x3d48352038ed9838` | **PASS** |
| 2-Core | Cores 0–1 | 10 / 10 | 100% | `0xf335fac86b8de36d` | **PASS** |
| 4-Core | Cores 0–3 | 10 / 10 | 100% | `0x6ccc38a4cb3f6467` | **PASS** |
| 8-Core | Cores 0–7 | 10 / 10 | 100% | `0x8529d89200e7f200` | **PASS** |

In every repetition across all topologies:
- Global instruction retirement sequences matched identically (`rep_ret == rec_ret`).
- Every virtual core's architectural PC, retirement count, and state hash matched with 100% byte fidelity.
- Shared memory CRC32C matched across all 16 MiB of RAM.

---

## 2. Exclusive Monitor Invalidation

ARMv8-A architecture requires that any store to physical memory by an external bus master (such as DMA) clears all exclusive monitor reservations holding reservations in that granule.

### Verification Scenario
1. Core 0 performs `load_exclusive` on target granule `0x30000`.
2. Core 1 initiates DMA fill to `0x30000`.
3. Live execution verifies:
   - Target memory updated with fill pattern.
   - Core 0 exclusive reservation invalidated (`is_reservation_valid(0, 0x30000) == false`).
4. Replay execution verifies:
   - Target memory updated with exact pattern without invoking live DMA engine.
   - Core 0 reservation invalidated during replay cursor processing.
   - Final canonical cluster hash matches live execution:
     `0x0d5b7cccd8c0a163 == 0x0d5b7cccd8c0a163`.
