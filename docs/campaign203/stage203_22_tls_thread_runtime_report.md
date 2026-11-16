# WinIOS Campaign 203 Stage 203.22: TLS and Thread Runtime Foundation Report

**Date:** 2026-11-16  
**Status:** COMPLETE (PASS)  
**Target Device Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Environment:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`)  
**Test Suite:** `stage203_22_tls_thread_runtime_tests` (Release & Debug)

---

## 1. Executive Summary

Stage 203.22 implements and verifies the Darwin thread runtime and thread-local storage (TLS) architecture in `DarwinThreadRuntime`, integrating architectural system registers `TPIDRRO_EL0` and `TPIDR_EL0`, pthread structure offsets, TSD (Thread Specific Data) slot alignments, and Mach-O TLV resolution.

Key architectural features verified:
1. **Darwin Pthread Structure & TSD Slot Offsets:** Formats `DarwinPthreadHeader` (184 bytes) in guest virtual memory:
   - `TSD[0]` (offset `0x00`): Self-pointer to `struct _opaque_pthread_t` (`pthread_self()`).
   - `TSD[1]` (offset `0x08`): Pointer to `errno_val` integer (`*__error()`).
   - `TSD[2]` (offset `0x10`): Mach MIG reply port name.
   - `TSD[3]` (offset `0x18`): Mach thread self port name.
   - `TSD[4]` (offset `0x20`): Default QoS class.
   - Header metadata: `magic = 0x50544852445F5400ULL` ("PTHRD_T\0"), `thread_id`, `stack_base`, `stack_size`, `tls_base`, and `tls_size`.
2. **Architectural System Register Emulation & Privileges:**
   - `TPIDRRO_EL0` (0x5E83): Read-only at EL0 via `MRS X0, TPIDRRO_EL0` (0xD53BD060); write attempts via `MSR TPIDRRO_EL0, X0` at EL0 are strictly rejected, while EL1 kernel writes succeed.
   - `TPIDR_EL0` (0x5E82): Read/write at EL0 via `MRS` (0xD53BD041) and `MSR` (0xD51BD041), providing the user-accessible TLS pointer.
3. **Synthetic Guest Instruction Sequences:**
   - `pthread_self()` sequence: `mrs x0, TPIDRRO_EL0; ldr x0, [x0, #0]` resolves the exact `pthread_t` address into `X0`.
   - `*__error()` sequence: `mrs x0, TPIDRRO_EL0; ldr x1, [x0, #8]; ldr w0, [x1, #0]` dereferences thread-local `errno` into `W0`.
4. **Mach-O TLV (Thread Local Variable) Resolution:** Correctly evaluates `DarwinTlvDescriptorRaw` descriptors using both `key = 0` (TPIDR_EL0-based) and `key = 1` (TPIDRRO_EL0-based) relative offsets.
5. **Multi-Thread Context Switching:** Verifies proper register switching and TLS isolation between distinct guest threads.

---

## 2. Test Execution Matrix

| Test Case | Description | Result (Release) | Result (Debug) |
|---|---|---|---|
| `test_darwin_pthread_structure_offsets` | Pthread struct layout, 184-byte size, TSD slot offsets, and errno update | PASS | PASS |
| `test_sysreg_tpidr_emulation_and_privileges` | TPIDRRO_EL0 / TPIDR_EL0 EL0/EL1 privilege rules and MSR/MRS execution | PASS | PASS |
| `test_guest_instruction_sequences` | CPU stepping of `pthread_self()` and `*__error()` instruction sequences | PASS | PASS |
| `test_macho_tlv_resolution` | TLV descriptor resolution and memory dereferencing | PASS | PASS |
| `test_thread_context_switching_isolation` | Multi-thread register preservation and context isolation | PASS | PASS |

---

## 3. Verification Details

All tests compiled under `/W4 /WX /permissive-` with 0 warnings and executed with 0 failures in both Release and Debug configurations.
