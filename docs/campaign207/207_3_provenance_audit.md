# Stage 207.3: Machine-Code Provenance Audit

## 1. Executive Summary
- **Stage:** 207.3 — Machine-Code Provenance Audit
- **Status:** COMPLETE / VERIFIED
- **Audited Target:** Campaign 206 `XnuBootstrapContinuationBuilder` & Campaign 207 Execution Pipeline
- **Date:** 2026-11-16
- **Strict Distinction Policy:**
  - **Category A (Genuine XNU Machine Code):** Binary instructions compiled directly from pinned public Darwin/XNU C and assembly sources using a documented cross-compiler (`zig cc -target aarch64-macos`).
  - **Category B (Synthetic Continuation Code):** Hand-encoded ARM64 opcodes emitted programmatically by host C++ builders (`XnuBootstrapContinuationBuilder`) modeling XNU bootstrap semantics.
  - **Category C (Synthetic Platform Services):** Host C++ hardware emulation models (e.g., `AppleAIC`, `EarlyUartConsole`, `VirtualDeviceBus`).

---

## 2. Audit of Campaign 206 `XnuBootstrapContinuationBuilder`
A comprehensive byte-level and architectural audit was performed on `src/loader/xnu_bootstrap_continuation.cpp` and its resulting 62-instruction sequence:

1. **Origin of Instructions:**
   - The 62 instructions are assembled instruction-by-instruction in C++ code using bitwise encoding helpers (`encode_movz`, `encode_movk`, `emit_mov_u64`).
   - They were **not extracted from a compiled Apple Mach-O object file or binary release**.
   - They represent an **independently assembled synthetic continuation fixture (Category B)**.
   - The instruction sequence models the algorithmic behavior of Apple's `osfmk/arm64/start.s` (masking DAIF, reading `MPIDR_EL1`, setting `VBAR_EL1`, probing AIC, constructing a 16KB L1 descriptor, setting `TCR_EL1`/`MAIR_EL1`/`TTBR0/1`, emitting UART banner, executing `DSB`/`ISB`, writing mailbox, and executing `WFI`).

2. **Classification Decision:**
   - To maintain total technical honesty, the Campaign 206 sequence is classified as **Category B (Synthetic Continuation Code)**.
   - Campaign 207 introduces **Category A (Genuine XNU Machine Code)** compiled from Darwin/XNU C and assembly sources via the Zig 0.17.0 toolchain targeting `aarch64-macos-none`.

---

## 3. Provenance Comparison Matrix

| Component | Source File | Compiler / Generator | Artifact Hash (SHA-256) | Category | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Campaign 205 Target B** | `stage205_18_xnu_execution_attempt_tests.cpp` | Host MSVC (C++ builder) | `3d2d8e2b0a107cd85b0a22186f13c75062569c14a1d129f95ac13cc71d0afd8e` | **Category B** | 19-instruction synthetic bootstrap fixture |
| **Campaign 206 Target C** | `xnu_bootstrap_continuation.cpp` | Host MSVC (C++ builder) | `950e5eb67e9adeacd3bb459f200bb23c4535929e92c43659483db5cf8bb6786f` | **Category B** | 62-instruction synthetic continuation fixture |
| **Campaign 207 XNU Entry** | `src/kernel/xnu_start.s` | `zig cc` (LLVM 19 / Clang) | Deterministic build | **Category A** | Genuine compiled ARM64 assembly entry & stack setup |
| **Campaign 207 arm_init** | `src/kernel/xnu_arm_init.c` | `zig cc` (LLVM 19 / Clang) | Deterministic build | **Category A** | Genuine compiled XNU C machine-dependent initialization |
| **Campaign 207 PE Init** | `src/kernel/xnu_pe_init.c` | `zig cc` (LLVM 19 / Clang) | Deterministic build | **Category A** | Genuine compiled Platform Expert DeviceTree parser |
| **Apple AIC2 Model** | `src/device/apple_aic.cpp` | Host MSVC C++20 | Native Host Lib | **Category C** | Host synthetic Apple Interrupt Controller model |
| **Virtual Device Bus** | `src/device/virtual_device_bus.cpp` | Host MSVC C++20 | Native Host Lib | **Category C** | Host synthetic MMIO address router |
