# WinIOS — Virtual Device & Replay Capability Matrix

## 1. Capability Invariants

| Configuration Key | Type | Default | Safety Invariant |
|---|---|---|---|
| `allow_virtual_devices` | `bool` | `false` | Must be explicitly enabled to instantiate MMIO devices |
| `allow_virtual_dma` | `bool` | `false` | Must be explicitly enabled to allow descriptor execution |
| `allow_dma_recording` | `bool` | `false` | Enables SNDL v2 DMA event recording |
| `allow_dma_replay` | `bool` | `false` | Strict gating: Replaying recorded DMA requires explicit opt-in AND `allow_dma_recording` |
| `allow_uncaptured_dma` | `bool` | `false` | **Permanently prohibited**. Setting to `true` causes immediate replay rejection (`UnsupportedConfiguration`) |
| `allow_unvirtualized_wall_clock` | `bool` | `false` | **Permanently prohibited**. BLK-SMP-04 safety enforcement |

## 2. Capability Configuration Interactions
- `allow_dma_replay = true` with `allow_dma_recording = false` -> REJECTED (`UnsupportedConfiguration`).
- `allow_uncaptured_dma = true` -> REJECTED (`UnsupportedConfiguration`).
- Default configuration (`allow_dma_replay = false`, `allow_uncaptured_dma = false`) ensures 100% backward compatibility for all existing 1-, 2-, 4-, and 8-core replay workloads.
