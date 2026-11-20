# Stage 241.9: Address-Space and ASID Isolation

## 1. ASID Isolation Guarantees
- Each user process is allocated a unique hardware Address Space ID (ASID) stored in `TTBR0_EL1` (bits 48-63).
- Translations are tagged with the active ASID, preventing TLB hit collisions across context switches.
- Memory isolation verified: two distinct tasks mapping identical virtual addresses (e.g. `0x20000`) translate to distinct physical pages in DRAM. Writes from Task 1 have zero impact on Task 2.
