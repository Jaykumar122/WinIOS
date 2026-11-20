# Stage 241.4: Load/Store and Guest Memory Semantics

## 1. Addressing Modes & Boundary Checks
Audited the guest memory translation interface (`UserAddressSpace` and `MMU`):

- **Addressing Modes**:
  - Immediate offset: `LDR/STR Xt, [Xn, #offset]`
  - Pre-indexed: `LDR/STR Xt, [Xn, #offset]!`
  - Post-indexed: `LDR/STR Xt, [Xn], #offset`
  - Register pair: `LDP/STP Xt1, Xt2, [Xn, #offset]`
- **Alignment & Granule Enforcement**:
  - Requires 16 KB page alignment for all mapped virtual address segments.
  - Unaligned memory accesses crossing page boundaries are validated against both page descriptors.
- **Fault Containment**:
  - Access to unmapped memory safely returns translation failure and asserts Data Abort (`ESR_EL1 = 0x92000007`).
  - Partial writes cannot corrupt adjacent physical pages.
- **Unimplemented Features Explicitly Documented**:
  - Exclusive load/store (`LDXR`/`STXR`) pair tracking across SMP cores is modeled in core tests but not exposed to freestanding EL0 guest binaries.
  - Acquire/Release atomics (`LDAR`/`STLR`) fall back to sequentially consistent barrier operations.
