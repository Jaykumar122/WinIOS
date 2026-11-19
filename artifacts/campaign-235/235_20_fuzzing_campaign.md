# Stage 235.20: Fuzzing Infrastructure & Corpus Expansion

## Executive Summary

Stage 235.20 expands automated fuzz testing across three security boundaries in WinIOS:
1. Application package manifest parsers (`winios::packaging::AppPackage`).
2. Mach-O binary and load command inspectors (`winios::loader::AppInspector`).
3. Freestanding guest userspace syscall dispatchers (`winios::kernel::UserSyscallDispatcher`).

The campaign introduced an exception-handling boundary inside `UserSyscallDispatcher::handle_syscall`, ensuring that guest-supplied oversized buffer lengths or invalid structures returning standard C++ library exceptions (`std::bad_alloc`, `std::length_error`, `std::out_of_range`) are converted into deterministic kernel error codes (`kErrnoEINVAL`) rather than terminating the host emulation process.

## Fuzzing Targets & Invariants

1. **Package Manifest & Archive Fuzzing (`AppPackage::unpack`)**:
   - Seed: Valid WIOSPKG3 package with manifest, files, and signatures.
   - Mutations: Bit flips, byte swaps, arithmetic deltas, boundary bytes, random overwrites.
   - Outcome: 1,000 iterations completed. 683 valid mutations safely unpacked; 317 malformed mutations safely rejected. Zero crashes, memory leaks, or unhandled exceptions.

2. **Mach-O Header & Load Command Fuzzing (`AppInspector::inspect_binary`)**:
   - Seed: Minimal freestanding ARM64 Mach-O executable.
   - Target: `LC_SEGMENT_64`, `LC_MAIN`, section headers, magic bytes, CPU type/subtype headers.
   - Outcome: 1,000 iterations completed. 997 valid mutations inspected with confirmed W^X cleanliness; 3 malformed mutations cleanly rejected with populated diagnostic reasons. Zero crashes.

3. **Syscall Argument & Dispatch Fuzzing (`UserSyscallDispatcher::handle_syscall`)**:
   - Seed: Randomized syscall IDs (0 to 600) and arbitrary 64-bit integers for `X0` through `X5` registers.
   - Defense: Enclosed core switch dispatcher in host exception guard, intercepting any guest-triggered standard exceptions.
   - Outcome: 1,000 randomized invocations processed safely. Zero host crashes or unhandled faults.

## Test Results

- **Test Suite**: `stage235_20_fuzzing_tests`
- **Execution Times**:
  - `RelWithDebInfo`: Passed (0.05s)
  - `Debug`: Passed (0.07s)
- **Status**: 100% passed across both configurations.
