# Stage 195.3 — Cooperative Context-Switch Verification

## 1. Executive Summary

Stage 195.3 verified the architectural correctness of cooperative CPU context switching in WinIOS. It validated AAPCS64 callee-saved register handling (`X19–X28`, `FP`, `LR`), stack pointer preservation, stack canary isolation, return address preservation, and target PC restoration across multiple threads.

---

## 2. Workload & Test Implementation

### 2.1 Three-Thread Cooperative Ring Workload (`guest_cooperative_3threads.S`)
- **Source:** `tests/artifacts/src/guest_cooperative_3threads.S`
- **Binary:** `tests/artifacts/bin/guest_cooperative_3threads` (51,064 bytes, Mach-O `MH_EXECUTE`, `CPU_TYPE_ARM64`)
- **Compilation Toolchain:** Zig 0.17.0 (`-target aarch64-macos -nostdlib -Wl,-pagezero_size,0`)
- **Protocol:**
  - Thread 1 establishes unique callee-saved registers (`X19=0x1111...1111`, `X20=0x1120...1120`, `X28=0x1128...1128`) and pushes stack canary `0xCAFEBABE11111111`.
  - Thread 1 calls `cpu_switch_to(TCB1, TCB2)`.
  - Thread 2 establishes unique registers (`X19=0x2222...2222`, `X28=0x2228...2228`) and pushes canary `0xCAFEBABE22222222`.
  - Thread 2 calls `cpu_switch_to(TCB2, TCB3)`.
  - Thread 3 establishes unique registers (`X19=0x3333...3333`, `X28=0x3328...3328`) and pushes canary `0xCAFEBABE33333333`.
  - Thread 3 calls `cpu_switch_to(TCB3, TCB1)`.
  - Thread 1 resumes after its switch, pops its stack canary, validates register contents, marks pass status, and hands off to Thread 2.
  - Thread 2 resumes, validates its canary and registers, marks pass, and hands off to Thread 3.
  - Thread 3 resumes, validates its canary and registers, marks pass, and completes execution.

### 2.2 Test Results
- Suite: `stage195_3_cooperative_switch_tests`
- Retired Guest Instructions: 230 instructions.
- Results:
  - `test_three_thread_cooperative_ring_guest_execution`: PASSED (3/3 threads verified stack & GPRs).
  - `test_host_thread_manager_cooperative_switching`: PASSED.
  - `test_architectural_context_capture_and_restore`: PASSED.

---

## 3. Evidence Classification
- `GUEST_EL1`: 230 authentic instructions retired in guest EL1.
- `GUEST_CONTEXT_SWITCH`: Direct register save and restore via `cpu_switch_to` in guest memory.
- `SYNTHETIC_KERNEL`: Freestanding Mach-O binary compiled via Zig 0.17.0.
