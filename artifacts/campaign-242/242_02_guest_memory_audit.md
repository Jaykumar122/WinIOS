# Stage 242.2: Guest Memory Architecture and Address Translation Audit

**Campaign**: 242
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Audited guest physical memory, virtual address space, 16KB page granule descriptors, and translation mechanisms:

- **Page Granule**: Fixed 16 KB (0x4000) granule for AArch64 guest address translation.
- **Virtual Address Limits**:
  - `StandardGeneral`: 32 GiB user address space limit (`0x800000000ULL`).
  - `StrictSandbox`: 2 GiB user sandbox limit (`0x80000000ULL`).
- **Permissions**: AP/PXN/UXN mapped through `PagePermissions` (`UserRead`, `UserReadWrite`, `UserExecute`).
- **Translation Structure**: Two-level translation table (L2 root and L3 leaf page tables) allocating 16KB pages.
- **Hardware vs Emulation Boundaries**: Windows flat guest RAM backed by `Memory` with bounds checking.
