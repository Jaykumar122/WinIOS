# DMA Capability Restrictions & BLK-SMP-03 Security Policy

## 1. Blocker Status (BLK-SMP-03)
BLK-SMP-03 remains ACTIVE and PROTECTED. The blocker specifies that asynchronous host DMA writes are not captured in the deterministic event stream.

## 2. Capability Enforcement
To prevent non-deterministic memory mutations from corrupting replay state:
1. `allow_uncaptured_dma` in `SmpReplayCapabilityConfig` defaults to `false`.
2. Setting `allow_uncaptured_dma = true` triggers immediate rejection with:
   ```
   ReplayErrorCode::UnsupportedConfiguration
   "BLK-SMP-03 violation: uncaptured host DMA replay is unsupported."
   ```
3. Current guest environment has no DMA-capable peripherals or asynchronous host device threads, ensuring complete determinism in all current workloads.
