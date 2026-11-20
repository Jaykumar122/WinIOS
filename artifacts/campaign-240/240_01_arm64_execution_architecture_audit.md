# Stage 240.1: ARM64 Execution Architecture Audit

## 1. Execution Pipeline Architectural Map
The WinIOS execution pipeline connects host OS services to the guest application execution environment through strict boundaries:

1. **Mach-O Admission & Inspection**:
   - Headers: `mach_header_64` (Magic: `MH_MAGIC_64` `0xFEEDFACF`, CPU Type: `CPU_TYPE_ARM64` `0x0100000C`).
   - Load Commands: Parse `LC_SEGMENT_64`, `LC_MAIN`, `LC_UNIXTHREAD`. Reject unsupported dynamic linking or encrypted segments.
2. **Guest Address Space Creation**:
   - Allocate isolated page table root / ASID for the guest task.
   - Enforce 16 KB page alignment and granule boundaries.
   - Map segments with W^X enforcement (`PAGE_EXECUTE_READ` vs `PAGE_READWRITE`).
3. **CPU Architectural State Initialization**:
   - General-purpose registers `X0` - `X30` zeroed, except `SP_EL0` pointing to stack top and `PC` pointing to entry point.
   - System registers (`PSTATE`, `ESR_EL1`, `FAR_EL1`, `TPIDR_EL0`).
4. **Instruction Fetch & Decode**:
   - Fetch 32-bit little-endian ARM64 instruction word at `PC`.
   - Dispatch to category decoder (Data Processing, Memory, Branch, System).
5. **Syscall Boundary**:
   - `SVC #0` trap triggers EL1 exception.
   - Syscall dispatcher decodes ABI (DarwinStrict vs FastABI / Category C).
   - Validates memory buffer pointers in guest virtual address space before copy.
6. **Application Lifecycle & Supervisor**:
   - Transitions tracked via `RuntimeLifecycleStateMachine`.
   - Resource quotas enforced via `AppResourceBudgetConfig`.
7. **Clean Termination**:
   - `SYS_exit` cleans up resources, releases physical pages, closes window handles, and updates session manager.
