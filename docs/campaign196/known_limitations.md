# Campaign 196 — Known Limitations & Research Boundaries

## 1. Emulation & Kernel Boundaries
- **Synthetic Kernel Services Only**: All kernel concurrency, synchronization primitives, memory allocators, thread control blocks, interrupt controllers, and schedulers are synthetic research models engineered for architecture verification. WinIOS does **NOT** execute genuine Apple XNU kernel binaries, Darwin Mach kernel source directly, or proprietary Apple Secure Enclave firmware.
- **Single-Host Contiguous Physical Memory Model**: The guest physical memory is hosted in a contiguous buffer managed by `Memory`. While multi-core concurrency is exercised via concurrent host worker threads and virtual CPU cores, hardware cache hierarchy details (MESI cache protocol state transitions, hardware cache coherency interconnects) are modeled via architectural reservation granule tracking (`SMPCluster`) and atomic host operations.
- **Litmus Test Relaxed Execution Window**: Litmus tests for weak-memory ordering (`stage196_6_acquire_release_tests.cpp`) verify architectural guarantees on sequentially consistent and weakly ordered operations. Because x86-64 host hardware enforces a Total Store Order (TSO) memory model, some non-TSO ARM64 relaxed reorderings (e.g., Load-Load or Store-Load without barriers) cannot be observed natively on an x86 host without synthetic memory access shuffle queues.

## 2. Hardware Register Coverage
- **Architectural GPRs and Vectors**: All 31 general-purpose registers (X0–X30), 32 128-bit vector registers (Q0–Q31), PC, SP_EL0, SP_EL1, SPSR_EL1, ELR_EL1, ESR_EL1, FAR_EL1, TTBR0_EL1, TCR_EL1, MAIR_EL1, SCTLR_EL1, CPACR_EL1, TPIDR_EL0, TPIDR_EL1, FPCR, and FPSR are preserved with 100% fidelity across context switches and exception boundaries.
- **ARMv8.6-A Neon vs SVE / SME**: Neon Advanced SIMD (128-bit) is the native vector architecture of the Apple A16 Bionic (T8120 SoC). SVE, SVE2, and SME are not present on Apple A16 Bionic hardware and are not modeled in WinIOS.
- **ARM64e PAC Keys**: Pointer Authentication Instructions (PAC/AUT) operate with synthetic cryptographic primitives from Campaign 192/193. PAC key registers are modeled globally but not yet isolated per-thread inside the 896-byte `ExceptionFrame`.

## 3. Test Fixtures & External Modules
- `inferno_integration_tests`: Maintained as safely skipped when the external Inferno research submodule is unconfigured, preserving repository integrity policies.
