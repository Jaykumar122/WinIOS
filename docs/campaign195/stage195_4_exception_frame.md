# Stage 195.4 — Asynchronous Exception Frame Architecture

## 1. Executive Summary

Stage 195.4 introduced and verified the canonical 896-byte `ExceptionFrame` structure for asynchronous exception and interrupt handling in WinIOS. The frame ensures strict 16-byte alignment, captures complete architectural register state, records exception syndromes (`ESR_EL1`), fault addresses (`FAR_EL1`), privilege levels, and preserves stack pointer isolation.

---

## 2. Architectural Structure

The `ExceptionFrame` structure is defined in `src/kernel/cpu_context.h`:
```cpp
struct alignas(16) ExceptionFrame {
    uint64_t magic;              // 0x000: 0x455843504652414D ("EXCPFRAM")
    uint64_t frame_size;         // 0x008: 896 bytes
    uint64_t elr_el1;            // 0x010: Interrupted PC
    uint64_t spsr_el1;           // 0x018: Saved PSTATE (NZCV, DAIF, mode)
    uint64_t esr_el1;            // 0x020: Exception Syndrome
    uint64_t far_el1;            // 0x028: Fault Address
    uint64_t sp_el0;             // 0x030: User stack pointer
    uint64_t sp_el1;             // 0x038: Original kernel stack pointer
    uint64_t tpidr_el0;          // 0x040: User thread ID pointer
    uint64_t tpidr_el1;          // 0x048: Kernel thread ID pointer
    uint64_t asid;               // 0x050: Address Space ID
    uint64_t ttbr0_el1;          // 0x058: User translation base
    uint32_t fpcr;               // 0x060: Floating-point control register
    uint32_t fpsr;               // 0x064: Floating-point status register
    uint32_t exception_origin_el;// 0x068: Originating execution level
    uint32_t simd_saved;         // 0x06C: 1 if Q0..Q31 were saved
    uint64_t gpr[31];            // 0x070 - 0x168: General purpose registers X0..X30
    uint64_t pad0;               // 0x168: 16-byte alignment pad
    GuestVectorReg q_regs[32];   // 0x170 - 0x370: 32x 128-bit vector registers Q0..Q31
    uint8_t reserved[16];        // 0x370 - 0x380: 16-byte alignment padding (total 896 bytes)
};
```

---

## 3. Test Verification
- Verified in `tests/unit/stage195_4_asynchronous_preemption_tests.cpp`.
- Verifies exact size (896 bytes), 16-byte alignment, push/pop mechanics, stack deduction, and register restoration.
- Evidence Classification: `HOST_MODEL` / `GUEST_EXCEPTION`.
