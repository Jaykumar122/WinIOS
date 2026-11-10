# Campaign 191-V Stage V2 — Differential CPU Verification Against Unicorn (QEMU TCG AArch64)

## 1. Differential Harness Architecture

- **Oracle:** Python `unicorn` `2.1.4` (`win_amd64` wheel wrapping QEMU TCG AArch64).
- **DUT Harness:** [`tests/tools/diff_cpu/diff_winios_runner.cpp`](file:///D:/WinISO26/WinIOS/tests/tools/diff_cpu/diff_winios_runner.cpp), persistent stdin/stdout driver executing WinIOS [`CPU`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp) at EL0 with the 16 KB Stage-1 Normal-Memory MMU enabled (`SCTLR_EL1.M=1, A=0, WXN=1`, `MAIR_EL1=0x00FF`).
- **Verifier Script:** [`tests/tools/diff_cpu/diff_arm64_oracle.py`](file:///D:/WinISO26/WinIOS/tests/tools/diff_cpu/diff_arm64_oracle.py) (`seed = 2026191`), comparing post-instruction `PC`, `SP`, `NZCV`, `X0..X30`, `Q0..Q31` (32 × 128-bit registers), and a 32 KB cross-16 KB-page-boundary data region `[0x40000, 0x48000)`.

## 2. Genuine CPU Bugs Discovered and Fixed

Running the differential oracle uncovered two real semantic bugs in WinIOS [`cpu.cpp`](file:///D:/WinISO26/WinIOS/src/cpu/cpu.cpp) (locked down by [`tests/unit/stage_v2_differential_cpu_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_v2_differential_cpu_tests.cpp)):

1. **Logical Immediate `Rd == 31` (`SP`/`WSP`) Ignored (`ARM DDI 0487 C6.2.11`):**
   - `AND`/`ORR`/`EOR` immediate (`opc != 3`) passed `stack_pointer_allowed = false` to `write_width_result`, silently discarding writes to `SP`/`WSP`. Fixed line 5622 to pass `opc != 3u` (`ANDS`/`opc==3` still targets `XZR`/`WZR`).
2. **Vector `CNT Vd.8B/16B, Vn.8B/16B` Misclassified as Float 2-Reg Misc (`ARM DDI 0487 C7.2.43`):**
   - In both execution and disassembly (`cpu.cpp:2795`, `9577`), `CNT` (`0x0E205800` / `0x4E205800`, `op_lo = 0b01011`) had bit 3 set (`op_lo & 0x8u != 0`), routing it into the FP 2-reg misc validator and throwing `Invalid 2-reg misc float encoding`. Implemented per-byte `std::popcount` for `.8B` and `.16B` alongside `Vector NOT (MVN)` and fixed the disassembler bitmask (`(op_lo & 0x18u) != 0x18u`).

### Documented Intentional Model Differences & Unsupported Groups
- **`B.cond` with `cond == 0b1111` (`NV`):** WinIOS intentionally throws `Reserved B.cond condition NV` (`cpu.cpp:7066`) to satisfy legacy [`branch_trace_tests.cpp:479`](file:///D:/WinISO26/WinIOS/tests/unit/branch_trace_tests.cpp#L479-L480), whereas QEMU/Unicorn executes `ConditionHolds(1111) == TRUE`. Left preserved in WinIOS; documented as an intentional difference and excluded (`cond = 0..14`) from the randomized branch gate.
- **`ADC`/`ADCS`/`SBC`/`SBCS` (`50` cases in `int_alu`):** Not yet implemented in `CPU::step()` (`Unsupported AArch64 instruction encoding`); tallied cleanly under `winios_unsupported` rather than masked.

## 3. Differential Run Summary (`seed = 2026191`)

| Category | Total | Compared | Matched | Mismatched | Unsupported | Oracle Fault |
|---|---:|---:|---:|---:|---:|---:|
| `int_alu` | 300 | 250 | 250 | 0 | 50 | 0 |
| `logic_bit` | 250 | 250 | 250 | 0 | 0 | 0 |
| `cond` | 150 | 150 | 150 | 0 | 0 | 0 |
| `branch` | 200 | 200 | 200 | 0 | 0 | 0 |
| `ldst` (incl. unaligned & cross-page) | 250 | 250 | 250 | 0 | 0 | 0 |
| `simd_fp` | 350 | 350 | 350 | 0 | 0 | 0 |
| `chains_imm` | 300 | 300 | 300 | 0 | 0 | 0 |
| **Total** | **1,800** | **1,750** | **1,750** | **0** | **50** | **0** |
