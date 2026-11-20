# Stage 239.8 Report: Guest Syscall Contract

## Objective
Establish a centralized, canonical contract registry for all registered guest system calls, defining syscall IDs, categories, expected parameter counts, parameter types, memory-mutation flags, and minimum ABI requirements.

## Implementation Details
1. **Canonical Registry Header**:
   - `src/kernel/syscall_registry.h`
   - Defines `SyscallDescriptor`:
     - `id`: uint32_t syscall number.
     - `name`: Human-readable identifier.
     - `category`: POSIX, Mach, Category C (WinIOS-specific), IPC, or VM.
     - `param_count`: Expected parameter count.
     - `mutates_memory`: Flag indicating whether the call writes to guest user memory.
     - `min_abi_version`: Minimum negotiated ABI version required.
2. **Standard Registry Entries**:
   - Complete inventory covering `kSysExit` (1), `kSysFork` (2), `kSysRead` (3), `kSysWrite` (4), `kSysOpen` (5), `kSysClose` (6), `kSysMmap` (197), `kSysMunmap` (73), `kSysWiniosGetAbiVersion` (507), `kSysWiniosCreateWindow` (508), `kSysWiniosDestroyWindow` (509), `kSysWiniosIpcSend` (510), `kSysWiniosIpcRecv` (511), `kSysWiniosGetBudget` (512), `kSysWiniosSetBudget` (513), and related platform calls.
3. **Validation Utility**:
   - `SyscallRegistry::lookup(id)` provides constant-time / bounded lookup for syscall validation prior to dispatch.

## Verification
- Unit test suite: `tests/unit/stage239_8_syscall_registry_contract_tests.cpp`.
- Verifies lookup correctness, unknown ID handling, category assignments, and parameter bound contracts.

## Status
Complete, verified, zero warnings.
