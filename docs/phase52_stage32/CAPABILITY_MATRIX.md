# WinIOS — Virtual Device & DMA Capability Matrix
## Topology Support, Capability Flags & Safety Invariants

---

## 1. Supported Topology Matrix

| Feature | 1-Core | 2-Core | 4-Core | 8-Core | >8-Core |
|---|---|---|---|---|---|
| Virtual Device Bus | Supported | Supported | Supported | Supported | Rejected |
| Virtual DMA Device | Supported | Supported | Supported | Supported | Rejected |
| DMA SNDL v2 Recording | Supported | Supported | Supported | Supported | Rejected |
| Companion `.dma` Store | Supported | Supported | Supported | Supported | Rejected |
| Deterministic Replay | Supported | Supported | Supported | Supported | Rejected |
| Exclusive Monitor Invalidation | Supported | Supported | Supported | Supported | Rejected |
| Maximum Core ID | 0 | 1 | 3 | 7 | N/A |

---

## 2. Replay Capability Flags

| Capability Flag | Default | Description | Safety Rule |
|---|---|---|---|
| `allow_dma_recording` | `true` | Allows SNDL v2 recorder to log DMA events. | Must be true when recording with virtual devices. |
| `allow_dma_replay` | `true` | Enables `SmpReplayCoordinator` to process DMA events. | If false, encountering a DMA event triggers divergence. |
| `allow_uncaptured_dma` | `false` | **STRICTLY FORBIDDEN**. Must remain `false`. | BLK-SMP-03 invariant: uncaptured host DMA is never permitted. |
| `allow_four_core_replay`| `false` | Explicit opt-in for 4-core replay. | Required for 4-core execution. |
| `allow_eight_core_replay`| `false` | Explicit opt-in for 8-core replay. | Required for 8-core execution. |

---

## 3. Resource Bounds and Limits

```
Maximum Transfer Length per Descriptor:  64 KiB (65,536 bytes)
Maximum Chained Descriptors:             16
Maximum Pending Submissions:             8
Inline Chunk Payload Threshold:          16 bytes
Descriptor Alignment:                    8 bytes (64-bit aligned)
Physical Address Space Limit:            Configured RAM capacity
```
Any violation of these limits results in immediate descriptor rejection with a deterministic error code written to the descriptor `status` field.
