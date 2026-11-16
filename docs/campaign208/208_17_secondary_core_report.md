# WinIOS Stage 208.17: Secondary Core Execution Truth & SMP Audit

## 1. Executive Summary
Campaign 207 verified that secondary cores evaluate `_start` and enter a parking loop. Stage 208 rigorously establishes whether secondary cores execute genuine guest instructions or merely transition through modeled parking states.

## 2. Technical Evaluation
- **Primary Core (Everest P-Core 0):**
  - MPIDR Affinity = 0 (`MPIDR_EL1.Aff0 = 0`).
  - Evaluates `mrs x20, mpidr_el1`, `and x20, x20, #0xff`, `cbnz x20, secondary_park`.
  - Condition `cbnz` is **NOT TAKEN** $\implies$ Proceeds into `start_first_cpu`, `arm_init`, `pe_arm_init`, and `PE_init_platform`.
- **Secondary Cores (Sawtooth E-Cores 1–5):**
  - MPIDR Affinity = 1 through 5 (`MPIDR_EL1.Aff0 != 0`).
  - Evaluates `cbnz x20, secondary_park` $\implies$ Condition **TAKEN**!
  - Branch target: `secondary_park` (`0x1BAC`).
  - Executed Instructions:
    1. `wfe` (Wait For Event, opcode `0xD503205F`) at `0x1BAC`.
    2. `b secondary_park` (opcode `0x17FFFFFF`) at `0x1BB0`.
  - State: Cores remain parked in low-power WFE spin-loop awaiting an architectural event or inter-processor interrupt (IPI).

## 3. Honest Architectural Distinction
- **What is NOT happening:** Secondary cores are NOT concurrently running Darwin kernel task schedulers or executing separate OS processes.
- **What IS happening:** Secondary cores are real guest `CPU` instances executing genuine compiled ARM64 instructions from the Mach-O binary (`wfe`, `b`), legitimately held in their architectural parking spin-table as dictated by Darwin's multi-core bootstrap protocol.
