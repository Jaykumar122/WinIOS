# Eight-Core Shared Memory Replay & Barrier Determinism

## 1. Shared Physical Memory Model
The `SmpSharedMemory` component provides a unified physical memory space shared across all 8 cores. Accesses from different cores are serialized deterministically through the single-host-thread scheduler quantum slices.

## 2. Memory Barrier Synchronization
AArch64 memory barriers (`DMB`, `DSB`, `ISB`) executed by any of the 8 cores emit `MemoryBarrierSync` events into the SNDL v2 stream.
- Event payload records the barrier type, shareability domain, and issuing core ID.
- During replay, the coordinator matches `MemoryBarrierSync` against the executed instruction sequence.
- Ensures cross-core memory visibility order is bit-for-bit identical to the recorded execution.

## 3. Test Verification
In Unit Test Group 7, an 8-core shared memory stress workload executed across 8 cores writing to overlapping cache lines with memory barriers. Replay reconstructed the identical final memory state across all 8 cores without divergence.
