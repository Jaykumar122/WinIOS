# WinIOS Phase 52 Stage 29 — Eight-Core Deterministic Replay Architecture

## 1. Architectural Scope & Objectives
Phase 52 Stage 29 establishes the multi-core deterministic replay engine for 8-core virtual execution clusters. Stage 28 generalized the SMP scheduler, shared physical memory, and recording systems to 8 cores while strictly rejecting 8-core replay. Stage 29 lifts this restriction under rigorous capability gating, delivering:

1. **Eight-Core Replay Coordinator (`SmpReplayCoordinator`)**: Full coordination of 8 virtual cores, scheduling boundaries, baseline restoration, and canonical state verification.
2. **Dual-Level Instruction Retirement Parity**: Global retirement sequence and per-core retirement counters tracked and verified against SNDL v2 records.
3. **Multi-Stream Event Synchronization**: Deterministic replay of core lifecycle transitions, scheduler slices, shared memory barriers, exclusive monitor locks, interrupts, virtual timers, MMIO reads, and entropy inputs across core IDs 0–7.
4. **Strict Dual Opt-in Capability Gate**: Requiring both `allow_experimental_smp = true` AND `allow_eight_core_replay = true`.
5. **Adversarial Hardening & Diagnostic Root-Cause Telemetry**: 14 failure injection groups verifying robust rejection and informative `SmpReplayDivergence` payloads.
6. **Bit-for-Bit Deterministic Invariance**: 10 repeated 8-core replay runs producing canonical hash `0x959c27b5d4286ffa`.

## 2. Component Architecture

```
+-----------------------------------------------------------------------+
|                       ReplaySession / CLI                             |
|       (Dual Opt-in Gating: allow_experimental_smp && eight_core)      |
+-----------------------------------+-----------------------------------+
                                    |
                                    v
+-----------------------------------------------------------------------+
|                       SmpReplayCoordinator                           |
|  - Restores 8-Core .wsnap v2 Snapshot                                  |
|  - Manages SmpReplayEventCursor                                       |
|  - Drives SmpScheduler Slices & Verifies Quantum Retirement           |
|  - Synchronizes Multi-Core Events (IDs 0..7)                          |
+---------+-------------------------+-------------------------+---------+
          |                         |                         |
          v                         v                         v
+-------------------+     +-------------------+     +-------------------+
|  SmpSharedMemory  |     |   SmpScheduler    |     |SmpExclusiveMonitor|
|  Unified Physical |     | Single Host-Thread|     |  8-Core Lock &    |
|   Address Space   |     |  Quantum Stepper  |     | Reservation Mgmt  |
+-------------------+     +-------------------+     +-------------------+
          |                         |                         |
          +------------+------------+------------+------------+
                       |                         |
                       v                         v
            +---------------------+   +---------------------+
            | SmpInterruptRouter  |   |     VirtualTime     |
            | PPI / SPI / SGI     |   | Deterministic Clock |
            | 8-Core Route & Ack  |   | (CNTPCT / CNTVCT)   |
            +---------------------+   +---------------------+
```

## 3. Capability Gating & BLK-SMP-03 Protection
Replay authorization strictly requires:
- `allow_experimental_smp = true`
- `allow_eight_core_replay = true`
- `allow_uncaptured_dma = false` (BLK-SMP-03 active and enforced)
- `clock_source != UnsupportedHostClock` && `allow_unvirtualized_wall_clock = false` (BLK-SMP-04 enforced)

Topologies beyond 8 cores (>8) are rejected unconditionally.
