# Stage 241.7: Mach-O Segment Mapping and 16 KB Granule

## 1. 16 KB Page Granule Architecture
The target research profile models an Apple A16 Bionic (`T8120`) SoC, which operates on a strict 16 KB (`0x4000`) page granule:

- **Segment Alignment**: Every loadable Mach-O segment (`__PAGEZERO`, `__TEXT`, `__DATA`, `__DATA_CONST`) must have `vmaddr` and `vmsize` aligned to 16 KB boundaries.
- **`__PAGEZERO` Guard**:
  - `vmaddr = 0x0`, `vmsize = 0x10000` (64 KiB).
  - Enforced `VM_PROT_NONE` (no read, write, or execute permissions).
  - Completely unmapped in page tables to deterministically trap null pointer dereferences.
- **Segment Overlap Prevention**:
  - The loader tracks mapped address intervals and rejects any overlapping virtual address ranges.
