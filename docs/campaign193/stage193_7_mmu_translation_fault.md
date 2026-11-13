# Stage 193.7 — MMU Translation and Fault Integration

## 1. Objectives & Scope
Stage 193.7 validates the Stage-1 16KB-granule MMU implementation and its integration with the architectural exception handling subsystem:
- 16KB translation granule geometry with standard Darwin 36-bit userspace VA (`T0SZ = 28`, `TG0 = 2`).
- Root table selection (Level 2 table root for 36-bit VA with 16KB granule).
- Translation fault levels: Level 2 translation fault (FSC `0x06`), Level 3 translation fault (FSC `0x07`).
- Access Flag validation: L3 descriptor with `AF=0` triggering Access Flag fault (FSC `0x0B`).
- Permission fault handling: Read-only pages rejecting write attempts with Permission fault level 3 (FSC `0x0F`).
- Architectural exception delivery on unprivileged EL0 guest accesses to EL1 kernel memory:
  - Vector jump to `VBAR_EL1 + 0x400` (Lower EL AArch64 Synchronous).
  - Exception class `0x24` (`DataAbortLower`).
  - `FAR_EL1` populated with the exact faulting address.
  - `WnR = 0` for loads.
  - Destination register preservation on fault.
- Store permission fault atomicity: failed store operations do not partially modify target memory (all-or-nothing guarantee).

## 2. Implementation & Test Architecture
The test suite `tests/unit/stage193_7_mmu_translation_fault_tests.cpp` exercises:
1. `test_16kb_granule_translation_and_fault_levels`:
   - Configures `SCTLR_EL1.M = 1` and `TCR_EL1 = (2 << 14) | 28`.
   - Exercises invalid Level 2 descriptor, verifying `MmuFaultType::Translation`, level 2, FSC `0x06`.
   - Chains Level 2 descriptor to Level 3 table with invalid entry, verifying level 3, FSC `0x07`.
2. `test_access_flag_and_permission_fault_integration`:
   - Validates descriptor with `AF=0` fails translation with `MmuFaultType::AccessFlag` and FSC `0x0B`.
   - Validates read-only descriptor (`AP[2:1] = 0b10`) succeeds on read, but fails on write with `MmuFaultType::Permission`, level 3, FSC `0x0F`.
3. `test_guest_unprivileged_el0_access_to_el1_kernel_memory`:
   - Maps user code at VA `0x1000` (PA `0x31000`) with user permissions (`AP[2:1] = 0b01`).
   - Maps kernel data at VA `0x4100` (PA `0x50100`) with EL1-only permissions (`AP[2:1] = 0b00`).
   - Executes guest EL0 instruction `LDR X0, [X1]` loading from `0x4100`.
   - Verifies CPU delivers synchronous Data Abort to `VBAR_EL1 + 0x400`, sets `CurrentEL = 1`, `ELR_EL1 = 0x1000`, `FAR_EL1 = 0x4100`, `ESR_EL1` EC `0x24` and DFSC `0x0F`, and leaves `X0` unmodified.
4. `test_guest_store_permission_fault_atomicity`:
   - Maps EL1 code page and read-only data page with sentinel value `0xDEADBEEFCAFE0011`.
   - Executes guest EL1 instruction `STR X0, [X1]` to read-only page.
   - Verifies Data Abort (Same EL) to `VBAR_EL1 + 0x200`, EC `0x25`, DFSC `0x0F`, `WnR = 1`, `FAR_EL1 = 0x4200`.
   - Verifies sentinel value in target memory is 100% unaltered.

## 3. Evidence & Verification Results
- `tests/unit/stage193_7_mmu_translation_fault_tests.cpp` (4/4 tests passed in both Debug and Release).
- Integrated with CTest suite target `stage193_7_mmu_translation_fault_tests`.
- Classification: `GUEST_EXCEPTION` / `GUEST_EL0` / `GUEST_EL1`.
