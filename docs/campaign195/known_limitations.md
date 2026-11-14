# Campaign 195 — Known Limitations & Research Boundaries

## 1. Emulation & Kernel Boundaries
- **Synthetic Kernel Services Only**: All kernel features (context switching, scheduler, preemption timer, wait queues, spinlocks, demand paging) are synthetic models engineered for architecture verification. WinIOS does **NOT** run genuine Apple XNU kernel binaries or proprietary Apple Secure Enclave firmware.
- **Single-Host Monolithic Memory**: The guest physical memory is mapped in a contiguous host `Memory` buffer. NUMA topologies and physical PCIe memory window translations are simulated at the software MMU layer.
- **Deterministic Replay Constraints**: Determinism is verified within single-process execution where host threading is coordinated sequentially via core scheduling ticks. Concurrent asynchronous host-thread execution relies on host OS scheduling and is evaluated via lock contention metrics.

## 2. Hardware Register Coverage
- **Core Registers**: X0–X30, Q0–Q31, PC, SP_EL0, SP_EL1, SPSR_EL1, ESR_EL1, FAR_EL1, TTBR0_EL1, TCR_EL1, MAIR_EL1, SCTLR_EL1, CPACR_EL1, TPIDR_EL0, TPIDR_EL1 are fully supported and preserved across context switches.
- **SVE / SVE2 / SME**: Scalable Vector Extensions and Scalable Matrix Extensions are not currently implemented, as Apple A16 Bionic (T8120) utilizes ARMv8.6-A Neon Advanced SIMD (128-bit vector width).
- **ARM64e PAC Keys**: Pointer Authentication keys (APIAKey, APDBKey, etc.) are emulated synthetically via PAC instructions from Campaign 192/193 but are not banked per-thread in the standard 896-byte `ExceptionFrame`.

## 3. Test Fixtures & External Modules
- `inferno_integration_tests`: Remains safely skipped when Inferno repository is not configured, maintaining repository integrity policy.
