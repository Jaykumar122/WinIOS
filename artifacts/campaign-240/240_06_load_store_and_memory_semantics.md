# Stage 240.6: Load/Store and Memory Semantics

## 1. Addressing Modes and Boundary Enforcement
The memory execution layer enforces strict boundary and alignment constraints:

1. **Addressing Modes**:
   - Immediate Offset: `LDR X0, [X1, #imm]`
   - Pre-indexed: `LDR X0, [X1, #imm]!` (base updated before access)
   - Post-indexed: `LDR X0, [X1], #imm` (base updated after access)
   - Register-pair: `STP X29, X30, [SP, #-16]!`
2. **Access Boundary Validation**:
   - Every read and write undergoes translation via `VirtualMemoryManager` and `PhysicalPageManager`.
   - Accesses to unmapped pages trigger synchronous Data Aborts (`ESR_EL1 = 0x92000007`, `FAR_EL1 = faulting_address`).
   - Write operations to read-only segments (`__TEXT`) trigger Permission Faults.
   - Cross-page accesses crossing unmapped boundaries are safely rejected.
