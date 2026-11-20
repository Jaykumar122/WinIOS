# Stage 240.9: System Register and Exception Behavior

## 1. Privilege Boundaries & Trap Handling
1. **User Mode (EL0) Privilege Restrictions**:
   - Guest applications run exclusively at EL0.
   - Privileged instructions (`MSR VBAR_EL1`, `TLBI`, `ERET`, `DC ZVA` outside EL0) trigger an Undefined Instruction exception (`ESR_EL1.EC = 0x00`).
2. **Supported System Registers**:
   - `NZCV`: Read/write condition flags.
   - `TPIDR_EL0`: Thread Local Storage (TLS) base pointer.
   - `CNTVCT_EL0`: Virtual timer counter read.
3. **Synchronous Exception Dispatch**:
   - `SVC #0` sets `ESR_EL1` to `0x56000000` (SVC in AArch64) and transfers control to `UserSyscallDispatcher`.
   - CPU state is preserved atomically, permitting non-destructive inspection and single-stepping.
