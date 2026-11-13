# WinIOS Campaign 192 — Stage 192.11: Thread-Local Storage & Thread Lifecycle Report

**Stage:** 192.11  
**Title:** Darwin Thread-Local Storage and Thread Lifecycle Conformance  
**Date:** 2026-11-13  
**Status:** COMPLETE (4/4 tests passed)  
**Host:** Windows 11 x64 | MSVC C++20 (`/W4 /WX /permissive-`)  
**Target:** ARM64 / Darwin Userspace (EL0)

---

## 1. Executive Summary

Stage 192.11 establishes Darwin userspace thread lifecycle management, thread-local storage (TLS) architecture via `TPIDR_EL0`, and context switching conformance. In Darwin ARM64 userspace, per-thread TLS bases are pointed to by the `TPIDR_EL0` system register, readable and writable in EL0 via `mrs x0, tpidr_el0` (`0xD53BD040`) and `msr tpidr_el0, x1` (`0xD51BD041`). 

We verified thread creation, 16-byte stack alignment invariants, context-switch preservation of `TPIDR_EL0` and general-purpose registers (`X0`..`X30`, `PC`, `SP`), thread termination semantics, and guest instruction access in EL0.

---

## 2. Implementation & Architectural Invariants

### 2.1 Darwin Thread Context & Register State
In `winios::runtime::DarwinThreadContext`:
- `tid`: Unique 64-bit thread identifier.
- `pc`, `sp`: Program counter and stack pointer (enforcing 16-byte alignment `(sp & 0xF) == 0`).
- `x[31]`: General-purpose registers `X0` through `X30`.
- `tpidr_el0`: Userspace thread-local storage base pointer.

### 2.2 Context Switching Engine Update
In `DarwinUserspaceEngine::save_thread_context` and `load_thread_context`:
```cpp
void DarwinUserspaceEngine::save_thread_context(DarwinThreadContext& ctx) {
    ctx.pc = cpu_.get_program_counter();
    ctx.sp = cpu_.get_sp();
    ctx.tpidr_el0 = cpu_.get_tpidr_el0();
    for (int i = 0; i < 31; ++i) {
        ctx.x[i] = cpu_.read_register(i);
    }
}

void DarwinUserspaceEngine::load_thread_context(const DarwinThreadContext& ctx) {
    cpu_.set_program_counter(ctx.pc);
    cpu_.set_sp(ctx.sp);
    cpu_.set_tpidr_el0(ctx.tpidr_el0);
    for (int i = 0; i < 31; ++i) {
        cpu_.write_register(i, ctx.x[i]);
    }
}
```

### 2.3 Guest ARM64 EL0 Instruction Access
Verified that `MRS` and `MSR` encodings for `TPIDR_EL0` execute without trapping in EL0:
- `mrs x0, tpidr_el0` (`0xD53BD040`): extracts `cpu_.get_tpidr_el0()` into target GPR.
- `msr tpidr_el0, x1` (`0xD51BD041`): updates `cpu_.set_tpidr_el0()` from source GPR.

---

## 3. Test Verification Results

Target: `stage_192_11_threads_tls_tests`

| Test Case | Description | Result |
| :--- | :--- | :--- |
| **Test 1** | Thread Creation, Register Initialization & 16-byte Stack Alignment | **PASS** |
| **Test 2** | Per-Thread TLS Isolation via TPIDR_EL0 Context Switching | **PASS** |
| **Test 3** | Thread Termination & Invalid TID Handling | **PASS** |
| **Test 4** | Guest ARM64 EL0 Instruction Access to TPIDR_EL0 (MRS/MSR) | **PASS** |

All 4 test cases passed with zero warnings and zero regressions.
