# Stage 239.6 Report: SDK ABI Conformance

## Objective
Audit and establish strict Category C ABI boundaries, struct alignments, calling conventions, and parameter bounds between the guest SDK (`guest-sdk/`) and the kernel user syscall dispatcher.

## Findings & Conformance Controls
1. **Calling Conventions**:
   - ARM64 AAPCS standard registers: arguments passed in `X0-X7`, return value returned in `X0` (or `X0-X1` for 128-bit results), syscall number passed in `X16` (DarwinStrict standard) or `X0` (Fast ABI fallback for host unit harnesses).
   - `UserSyscallDispatcher::handle_syscall` handles both conventions smoothly without ambiguity.
2. **Memory Alignment & Granule**:
   - iPhone 15 research profile (`iPhone15,4`, `D37AP`, `T8120`) requires strict 16 KB page granules (`kPageSize = 0x4000`).
   - Guest pointers passed to kernel entry points (`sys_winios_*`) undergo canonical address sanitization, bounds checking against guest address space `[0x10000, 0x00007FFFFFFFFFFF]`, and alignment validation.
3. **Struct Layout Integrity**:
   - All Category C shared structures (`WiniosWindowDesc`, `WiniosIpcMessage`, `WiniosSessionState`, `WiniosResourceBudget`) enforce explicit `#pragma pack` or naturally aligned 64-bit boundaries to guarantee identical memory layout across host MSVC C++20 and guest Zig 0.17.0.

## Status
Conforming and verified across all Category C system calls.
