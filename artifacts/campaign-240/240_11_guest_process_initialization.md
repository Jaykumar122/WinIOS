# Stage 240.11: Guest Process Initialization

## 1. Process Lifecycle Initialization Flow
1. **Admission & Security Verification**:
   - Verify Mach-O headers and segment sanity.
2. **Address Space Construction**:
   - Allocate task ASID, initialize page tables.
   - Map text and data segments.
   - Allocate and map stack region (`0x00060000 - 0x0007EFFF`).
3. **Initial Context Configuration**:
   - `PC = EntryPoint`
   - `SP_EL0 = 0x0007E000` (16-byte aligned)
   - General-purpose registers initialized to clean state.
4. **State Machine Transition**:
   - `AppLifecycleState`: `Created` -> `Admitted` -> `Loading` -> `Initialized`.
5. **Rollback Guarantee**:
   - On initialization failure, all allocated pages and task handles are rolled back atomically. Verified in test suite.
