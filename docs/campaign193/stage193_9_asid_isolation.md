# Stage 193.9 — ASID and Address-Space Isolation

## 1. Objectives & Scope
Stage 193.9 implements and validates bounded address-space and ASID isolation:
- Architectural ASID representation: extracting the 16-bit ASID from `TTBR0_EL1[63:48]` when `TCR_EL1.A1 == 0`, and from `TTBR1_EL1[63:48]` when `TCR_EL1.A1 == 1`.
- Address-space association and translation context selection.
- Cross-address-space isolation demonstrated with two guest contexts (Process A with ASID 1 and Process B with ASID 2) configured with deliberately conflicting virtual addresses (`user_code_va = 0x1000`, `user_data_va = 0x5000`).
- Proving each guest process executes guest code at EL0 and exclusively observes its own physical memory.
- TLB entry tagging with ASID identity and multi-ASID TLB co-existence.
- Context-switch behavior: switching `TTBR0_EL1` dynamically switches translation contexts, and restoring `TTBR0_EL1` cleanly restores context isolation.
- ASID invalidation behavior: `TLBI ASIDE1` targeting ASID 1 evicts only ASID 1 entries while leaving ASID 2 entries intact in the TLB.

## 2. Implementation & Test Architecture
The test suite `tests/unit/stage193_9_asid_isolation_tests.cpp` exercises:
1. `test_asid_representation_and_context_selection`:
   - Configures `TCR_EL1` with `A1 = 0` and programs `TTBR0_EL1` with ASID `0x0042` and `TTBR1_EL1` with ASID `0x0099`.
   - Translates non-global page and verifies cached TLB entry has `asid == 0x0042`.
   - Flips `TCR_EL1.A1 = 1`, translates non-global page, and verifies cached TLB entry has `asid == 0x0099`.
2. `test_two_guest_contexts_conflicting_virtual_addresses`:
   - Configures Process A (ASID 1): Page table at `0x40000`, code VA `0x1000` (PA `0x30000`), data VA `0x5000` (PA `0x50000`, value `0x1111222233334444`).
   - Configures Process B (ASID 2): Page table at `0x48000`, identical code VA `0x1000` (PA `0x34000`), identical data VA `0x5000` (PA `0x70000`, value `0x88889999AAAABBBB`).
   - Guest Execution Sequence:
     - Under Process A, EL0 executes `LDR X0, [X1]` (from VA `0x5000`): loads `0x1111222233334444`.
     - Kernel context switches to Process B by updating `TTBR0_EL1` to ASID 2 / table `0x48000`.
     - Under Process B, EL0 executes `LDR X0, [X1]` (from VA `0x5000`): loads `0x88889999AAAABBBB`.
     - Kernel switches back to Process A: EL0 re-executes `LDR X0, [X1]`: loads `0x1111222233334444`.
   - Confirms true physical isolation across identical guest virtual address spaces.
3. `test_tlb_asid_coexistence_and_tagging`:
   - Populates TLB with non-global Page 0 under ASID 1 and non-global Page 1 under ASID 2.
   - Verifies both entries simultaneously reside in the TLB with their respective ASID tags.
   - Issues `TLBI ASIDE1(1)`: evicts ASID 1 entry while preserving ASID 2 entry.

## 3. Evidence & Verification Results
- `tests/unit/stage193_9_asid_isolation_tests.cpp` (3/3 tests passed in both Debug and Release).
- CTest target: `stage193_9_asid_isolation_tests`.
- Classification: `GUEST_EL0` / `GUEST_EL1` / `HOST_MODEL`.
