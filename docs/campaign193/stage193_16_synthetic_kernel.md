# Stage 193.16 — Synthetic EL1 Kernel Runtime

## 1. Objectives & Scope
Stage 193.16 establishes a freestanding, independently compiled ARM64 guest kernel runtime (`synthetic_el1_kernel`) cross-compiled with Zig 0.17.0 (LLVM 22 backend) without reliance on proprietary Apple binaries:
- Kernel Artifact Provenance:
  - Source: `tests/artifacts/src/synthetic_el1_kernel.S`.
  - Binary target: `tests/artifacts/bin/synthetic_el1_kernel`.
  - Toolchain: Zig 0.17.0 (`zig cc -target aarch64-macos -nostdlib -fno-sanitize=undefined -fno-stack-protector "-Wl,-e,_start" "-Wl,-pagezero_size,0"`).
  - Exact binary size: 51,832 bytes.
  - Cryptographic SHA-256 hash: `ef08793bdf38817e06c8d37157d5b25ba94b959269a7965ad45a82d4c4a200cf`.
  - Classification: `SYNTHETIC_KERNEL` / `GUEST_EL1` / `GUEST_EL0` / `GUEST_EXCEPTION`.
- Architectural Vector Table:
  - Vector base table aligned at 2048 bytes (2KB).
  - Full 16-entry dispatch table for Current EL SP0, Current EL SPx, Lower EL AArch64, Lower EL AArch32.
- Full Kernel Boot & Privilege Demotion:
  - CPU boots at EL1 (`CurrentEL = 1`, `SPSel = 1`).
  - Kernel sets `VBAR_EL1` to vector table base.
  - Kernel sets `SP` to EL1 stack (`0x70000`) and `SP_EL0` to user stack (`0x80000`).
  - Kernel zeroes kernel data structures at `0x40000..0x40038`.
  - Kernel configures `ELR_EL1` to `_client_entry` and `SPSR_EL1` to `0` (EL0t).
  - Kernel executes `ERET`, dropping privilege to EL0 and branching to client code.
- EL0 Client Syscall Services:
  - Syscall 1 (`SYS_diag_print`): passes buffer and length in `X0`, `X1`, syscall number in `X8 = 1`. Kernel handler vectors at `VBAR_EL1 + 0x400`, elevates to EL1, transfers string to virtual console MMIO (`0x90000`), returns to EL0 via `ERET`.
  - Syscall 2 (`SYS_incr_counter`): requests increment of protected EL1 memory counter (`0x40000`). Kernel safely mutates state and returns updated count in `X0`.
  - Syscall 3 (`SYS_exit`): sets completion flag at `0x40008` = 1, returns status code 42 in `X0`.
- Page Fault Servicing:
  - Lower EL Data Abort (EC `0x24`) vectors to `VBAR_EL1 + 0x400`.
  - Kernel handler reads `FAR_EL1`, records faulting address at `0x40010`, increments fault count at `0x40018`, adjusts `ELR_EL1`, and returns via `ERET`.

## 2. Implementation & Test Architecture
The test suite `tests/unit/stage193_16_synthetic_kernel_tests.cpp` exercises:
1. `test_synthetic_kernel_artifact_provenance_and_hash`:
   - Validates existence, size (51,832 bytes), exact SHA-256 hash, and Mach-O header properties.
2. `test_synthetic_kernel_execution_and_privilege_transition`:
   - Loads binary via `MachOLoader::load_from_buffer`.
   - Boots CPU in EL1, steps kernel initialization, verifies transition to EL0 via `ERET`.
   - Executes EL0 client through all syscalls, captures "SyntheticKernel-OK\n" on virtual console, verifies counter = 2, completion flag = 1, exit status = 42.
   - Retires 178 genuine guest instructions through `CPU::step`.
3. `test_synthetic_kernel_page_fault_handling`:
   - Delivers architectural Lower EL Data Abort with `FAR_EL1 = 0x55000`.
   - Steps kernel handler, verifying `FAR_EL1` logging and clean `ERET` return to EL0.

## 3. Evidence & Verification Results
- `tests/unit/stage193_16_synthetic_kernel_tests.cpp` (3/3 tests passed in both Debug and Release).
- CTest target: `stage193_16_synthetic_kernel_tests`.
- Classification: `SYNTHETIC_KERNEL` / `GUEST_EL1` / `GUEST_EL0` / `GUEST_EXCEPTION`.
