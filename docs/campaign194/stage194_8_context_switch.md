# WINIOS CAMPAIGN 194: STAGE 194.8 REPORT
## Guest Context Switching

### 1. Executive Summary
Stage 194.8 delivers the low-level ARM64 architectural context-switch mechanism (`cpu_switch_to`) for the synthetic kernel runtime.
Key deliverables:
- **Assembly Implementation:** An authentic 22-instruction ARM64 subroutine (`tests/artifacts/src/guest_context_switch.S`) preserving AAPCS64 callee-saved registers (`X19..X30`), stack pointer (`SP`), and link register (`LR`/`PC`).
- **Binary Artifact:** Independently cross-compiled using Zig 0.17.0 (Clang 22.1.8) to Mach-O executable `tests/artifacts/bin/guest_context_switch` (50,088 bytes, SHA-256 `651bd7ed44dc0a2377ec1f124a27c65f255e1d6f22d4c22dde23b251dcb723b8`).
- **Machine Code Generator:** `ContextSwitch::emit_guest_switch_routine()` generating authentic machine code words for placement in guest memory.
- **Null `old_tcb` Handling:** `cbz x0, .Lload_new` branch enables zero-cost initial thread dispatching during kernel bootstrap without dereferencing null pointers.
- **Round-Trip Fidelity:** Tested on the virtual ARM64 CPU with bidirectional thread swapping, verifying memory writeback to the outgoing TCB and state activation on the incoming TCB.

---

### 2. Architecture & Design

#### 2.1 Subroutine Contract: `cpu_switch_to(old_tcb, new_tcb)`
- **Input Parameters:**
  - `X0`: Pointer to outgoing `GuestTcb` in guest RAM (or `0` during bootstrap).
  - `X1`: Pointer to incoming `GuestTcb` in guest RAM.
- **Save Outgoing Thread State (if `X0 != 0`):**
  ```arm64
  cbz     x0, .Lload_new
  mov     x2, sp
  str     x2, [x0, #0x28]        // Save SP to old_tcb->sp_el1
  str     x30, [x0, #0x30]       // Save LR to old_tcb->pc
  stp     x19, x20, [x0, #0xd8]  // Save callee-saved registers
  stp     x21, x22, [x0, #0xe8]
  stp     x23, x24, [x0, #0xf8]
  stp     x25, x26, [x0, #0x108]
  stp     x27, x28, [x0, #0x118]
  stp     x29, x30, [x0, #0x128]
  ```
- **Load Incoming Thread State:**
  ```arm64
  .Lload_new:
  ldr     x2, [x1, #0x28]        // Restore SP from new_tcb->sp_el1
  mov     sp, x2
  ldp     x19, x20, [x1, #0xd8]  // Restore callee-saved registers
  ldp     x21, x22, [x1, #0xe8]
  ldp     x23, x24, [x1, #0xf8]
  ldp     x25, x26, [x1, #0x108]
  ldp     x27, x28, [x1, #0x118]
  ldp     x29, x30, [x1, #0x128]
  ldr     x16, [x1, #0x30]       // Load target PC into scratch register
  mov     w2, #1
  str     w2, [x1, #0x10]        // Update new_tcb->state to Running (1)
  br      x16                    // Jump to target instruction
  ```

---

### 3. Verification & Results

Unit test suite `tests/unit/stage194_8_context_switch_tests.cpp` executes 5 test cases:
1. `test_switch_routine_machine_code_validity`: Validates instruction count (22 words) and key boundary opcodes (`CBZ`, `BR`).
2. `test_host_thread_switch`: Verifies high-level bidirectional thread switching, state transitions (Running <-> Ready), and lossless context retention.
3. `test_guest_assembly_context_switch_round_trip`: Steps virtual CPU through all 22 guest instructions, verifying memory updates in RAM for SP, PC, and X19..X30, and CPU register restoration.
4. `test_guest_bootstrap_initial_thread_switch`: Verifies null outgoing TCB dispatch path without faults.
5. `test_compiled_macho_binary_artifact`: Confirms independently cross-compiled Mach-O artifact existence, size (50,088 bytes), and SHA-256 integrity.

Both Release and Debug configurations build cleanly under `/W4 /WX /permissive-` and pass all 5 tests with zero errors.

---

### 4. Classification
- Source implementations: `GUEST_SCHEDULER` / `SYNTHETIC_KERNEL`
- Binary artifact: `GUEST_EL1`
- Simulation / host harness: `HOST_MODEL`
