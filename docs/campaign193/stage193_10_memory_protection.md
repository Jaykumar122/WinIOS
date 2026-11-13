# Stage 193.10 — EL0/EL1 Memory Protection

## 1. Objectives & Scope
Stage 193.10 validates privilege-aware memory protection mechanisms:
- EL0-accessible vs EL1-only memory: ensuring guest EL0 attempts to read or write EL1-only memory (`AP[2:1] = 0b00`) trap with `DataAbortLower` (EC `0x24`), `DFSC = 0x0F` (Permission fault level 3), accurate `WnR` indicator (0 for load, 1 for store), and `FAR_EL1` recording the faulting address.
- Permitted EL1 kernel access: verifying EL1 can perform read and write accesses to kernel memory without generating exceptions.
- Read-only protection: verifying pages mapped with read-only permissions (`AP[2:1] = 0b11`) allow reads from EL0 and EL1, while store operations trap to EL1 (at vector `+0x400` with EC `0x24` for EL0; at vector `+0x200` with EC `0x25` for EL1).
- Execute-Never protection (`UXN` and `PXN`):
  - Instructions fetched from a page with `UXN = 1` (bit 54) at EL0 trap with `InstructionAbortLower` (EC `0x20`) and `IFSC = 0x0F`.
  - Instructions fetched from a page with `PXN = 1` (bit 53) at EL1 trap with `InstructionAbortSame` (EC `0x21`) and `IFSC = 0x0F`.
- W^X policy (`SCTLR_EL1.WXN`): when enabled (bit 19 = 1), any writable page automatically has execute permission revoked, trapping execution attempts with an Instruction Abort.
- Dynamic permission changes and TLB synchronization: demonstrating that when an EL0 store traps on a read-only page, the EL1 exception handler can upgrade the page table descriptor to read-write, synchronize with `TLBI VAE1, Xt`, and resume EL0 to successfully complete the store.

## 2. Implementation & Test Architecture
The test suite `tests/unit/stage193_10_memory_protection_tests.cpp` exercises:
1. `test_el0_access_to_el1_only_memory_rejected`:
   - Guest EL0 executes `LDR` to kernel page -> traps with EC `0x24`, DFSC `0x0F`, `WnR = 0`, destination register unmodified.
   - Guest EL0 executes `STR` to kernel page -> traps with EC `0x24`, DFSC `0x0F`, `WnR = 1`, kernel memory untouched.
2. `test_el1_access_to_permitted_kernel_memory_succeeds`:
   - Guest EL1 executes `LDR` and `STR` to kernel page -> both instructions succeed, PC increments past each.
3. `test_read_only_protection_at_el0_and_el1`:
   - EL0 `LDR` succeeds; EL0 `STR` traps to `VBAR_EL1 + 0x400`, EC `0x24`, `WnR = 1`.
   - EL1 `LDR` succeeds; EL1 `STR` traps to `VBAR_EL1 + 0x200`, EC `0x25`, `WnR = 1`.
4. `test_execute_never_protection_at_el0_and_el1`:
   - EL0 attempts instruction fetch from `UXN` page -> traps with EC `0x20`, `IFSC = 0x0F`, `FAR_EL1 = PC`.
   - EL1 attempts instruction fetch from `PXN` page -> traps with EC `0x21`, `IFSC = 0x0F`, `FAR_EL1 = PC`.
5. `test_wxn_policy_enforcement`:
   - Enables `SCTLR_EL1.WXN`.
   - EL0 attempts instruction fetch on writable page -> traps with `InstructionAbortLower` (EC `0x20`), `IFSC = 0x0F`.
6. `test_dynamic_permission_changes_and_tlb_synchronization`:
   - EL0 store on read-only page traps.
   - Handler updates L3 descriptor to RW and calls `mmu.invalidate_tlb_va(va, asid, false)`.
   - EL0 retries instruction and succeeds.

## 3. Evidence & Verification Results
- `tests/unit/stage193_10_memory_protection_tests.cpp` (6/6 tests passed in both Debug and Release).
- CTest target: `stage193_10_memory_protection_tests`.
- Classification: `GUEST_EXCEPTION` / `GUEST_EL0` / `GUEST_EL1` / `HOST_MODEL`.
