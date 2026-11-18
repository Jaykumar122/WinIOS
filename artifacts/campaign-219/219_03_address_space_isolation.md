# WinIOS Campaign 219 — Stage 219.3: Address Space Isolation & Page Table Management

## 1. MMU & Page Table Architecture

WinIOS implements strict Stage-1 hardware address translation adhering to ARMv8-A architecture with a 16 KB translation granule:

- **Translation Granule**: 16 KB (`kPhysicalPageSize16KB = 16384` bytes).
- **Page Table Levels**: L0 (root table, 48-bit VA) through L3 (page table descriptors).
- **Base Registers**:
  - `TTBR0_EL1`: Holds the physical base address of the active user process's L2/L1 translation table.
  - `TTBR1_EL1`: Holds the kernel translation table base address (`0xFFFF800000000000`+).
  - `TCR_EL1`: Configured with `TG0 = 0b10` (16 KB granule) and `T0SZ = 28` (36-bit user address space, 64 GiB limit).

## 2. Address Space Isolation Guarantees

Every guest process maintains an autonomous `UserAddressSpace` instance with a unique physical root page table allocated from `PhysicalPageManager`:

```
Process A (PID 100):
   UserAddressSpace A -> Root L2 PA: 0x00040000
   VA 0x10000000 ------> Maps to PA 0x00100000 (Data: 0xAAAAAAAA)

Process B (PID 101):
   UserAddressSpace B -> Root L2 PA: 0x00044000
   VA 0x10000000 ------> Maps to PA 0x00104000 (Data: 0xBBBBBBBB)
```

### Verified Isolation Properties
1. **Identical Virtual Addresses, Independent Physical Frames**:
   Processes A and B can map identical virtual addresses (e.g., standard text base `0x100000000` or stack base `0x00000007FFFF0000`) without collision. Writes in Process A cannot modify memory in Process B.
2. **Translation Table Switching**:
   During context switching, the scheduler loads `cpu.get_mmu().set_ttbr0_el1(target_ttbr0)` and calls `cpu.get_mmu().invalidate_tlb_all()`. Hardware translation through the CPU MMU evaluates solely against the new process's page directory.
3. **Unmapped Address Fault Detection**:
   If Process A maps an address `0x20000000` that Process B does not, translation under Process B immediately triggers a translation fault (returns null / 0), preventing unauthorized cross-process snooping.
4. **Physical Page Reclamation**:
   Upon process termination and destruction of `UserAddressSpace`, all allocated L3 table pages, intermediate tables, and heap/stack frames recorded in `allocated_physical_pages_` are returned to `PhysicalPageManager`.

## 3. Test Verification

Verified by `stage219_3_address_space_isolation_tests.exe`:
- Multi-process distinct translation table allocation: `root_a != root_b`.
- Cross-process write isolation: Process A written `0xAAAAAAAA`, Process B written `0xBBBBBBBB`, read back verified.
- Hardware MMU translation switching: `MMU::translate()` validated under both TTBR0 configurations.
