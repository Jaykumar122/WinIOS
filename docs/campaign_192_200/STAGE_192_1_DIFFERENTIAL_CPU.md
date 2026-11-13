# WINIOS CAMPAIGN 192 — STAGE 192.1 REPORT
## ARM64 Differential CPU Validation Expansion & Arithmetic With Carry Implementation

**Campaign:** 192  
**Stage:** 192.1 (ARM64 Differential Validation Expansion)  
**Date:** 2026-11-13  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host Architecture:** Windows 11 x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**External Differential Oracle:** Unicorn Engine 2.1.4 (`arm64-darwin` / QEMU TCG AArch64 engine)  
**Verdict:** **PASSED (4,300 / 4,300 trials matched, 0 mismatches, 0 unsupported, 100.0% architectural conformance)**

---

## 1. Executive Summary

In Stage 192.1, the WinIOS guest CPU core was extended with full architectural support for ARM64 Add with Carry and Subtract with Carry instructions (`ADC`, `ADCS`, `SBC`, `SBCS`), closing the remaining gap observed in the baseline differential oracle.

Following implementation, the differential CPU test suite was expanded from the original 1,800 cases by adding **2,500 targeted, high-stress test cases** focusing on arithmetic carry propagation, extreme boundary operands, shifted/extended register arithmetic, all 16 condition codes across all 16 NZCV permutations, 32-bit destination zero-extension with SP vs XZR distinction, and SIMD/FP corner cases (narrowing `SHRN`, widening `SMULL`, and conditional select `FCSEL`).

Across all **4,300 differential test cases**, WinIOS executed step-for-step bit-for-bit identical to Unicorn Engine 2.1.4 without a single mismatch, unsupported instruction error, or crash.

---

## 2. Instruction Implementation: `ADC`, `ADCS`, `SBC`, `SBCS`

### 2.1 Encoding
The ARM64 Add/Subtract with Carry instruction family is encoded under Data-processing (2 source):
- **Opcode mask:** `0x1FE0FC00u` matching pattern `0x1A000000u`.
- **Bits:**
  - `sf` (bit 31): `0` for 32-bit (`W`), `1` for 64-bit (`X`).
  - `op` (bit 30): `0` for `ADC` / `ADCS`, `1` for `SBC` / `SBCS`.
  - `S` (bit 29): `0` for no flag update, `1` for condition flag update (`ADCS` / `SBCS`).
  - `rm` (bits 20-16): Second source register.
  - `rn` (bits 9-5): First source register.
  - `rd` (bits 4-0): Destination register. Note: Register 31 is architecturally `XZR`/`WZR`, **never** `SP`/`WSP`.

### 2.2 Architectural Flag Computation
Implementation in `src/cpu/cpu.cpp` (`arithmetic_with_carry_flags`):
```cpp
static std::uint32_t arithmetic_with_carry_flags(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t carry_in,
    std::uint64_t result,
    bool is64)
{
    const std::uint64_t mask = is64 ? ~0ULL : 0xFFFFFFFFULL;
    const std::uint64_t sign_bit = is64 ? (1ULL << 63u) : (1ULL << 31u);
    left &= mask;
    right &= mask;
    result &= mask;

    std::uint32_t nzcv = 0;
    if (result & sign_bit) nzcv |= CPU::kNegative;
    if (result == 0) nzcv |= CPU::kZero;

    // Carry out: sum1 = left + right; carry = (sum1 < left) || ((sum1 + carry_in) < sum1)
    const std::uint64_t sum1 = (left + right) & mask;
    const bool carry_out = (sum1 < left) || (((sum1 + carry_in) & mask) < sum1);
    if (carry_out) nzcv |= CPU::kCarry;

    // Overflow V: signs of inputs match each other but differ from sign of result
    const bool overflow = ((~(left ^ right) & (left ^ result) & sign_bit) != 0);
    if (overflow) nzcv |= CPU::kOverflow;

    return nzcv;
}
```

For subtraction (`SBC` / `SBCS`), the right-hand operand is inverted (`~right & mask`), and addition with carry is computed as `left + (~right) + C`.

---

## 3. C++ Regression Unit Tests

Added test suite `test_adc_adcs_sbc_sbcs()` to `tests/unit/stage_v2_differential_cpu_tests.cpp`:
1. `ADC W0, W1, W2` with $C=0$ ($10 + 20 + 0 = 30$) and $C=1$ ($10 + 20 + 1 = 31$).
2. `ADCS WZR, W1, W2` with $C=1$ ($0xFFFFFFFF + 0 + 1 = 0x100000000$): verifies zero result, sets $Z=1, C=1, N=0, V=0$, and verifies stack pointer $SP$ remains completely unmodified.
3. `ADCS X0, X1, X2` with 64-bit signed overflow ($0x7FFFFFFFFFFFFFFF + 1 + 0 = 0x8000000000000000$): verifies $N=1, V=1, C=0, Z=0$.
4. `SBC W0, W1, W2` with $C=1$ ($50 - 20 = 30$) and $C=0$ ($50 - 20 - 1 = 29$).
5. `SBCS X0, X1, X2` with borrow ($10 - 20 = -10$, $C=0, N=1$) and exact cancellation ($10 - 10 = 0$, $C=1, Z=1$).

Built with MSVC `/W4 /WX` in Release and Debug modes:
```
[Test 1] Logical immediate Rd=31 writes SP/WSP (opc!=3) vs XZR/WZR (opc==3)...
[Test 2] Vector CNT Vd.8B/16B, Vn.8B/16B & disassembly...
[Test 3] ADC, ADCS, SBC, SBCS 32/64-bit semantics, flags, and XZR/WZR destination...
ALL STAGE V2 DIFFERENTIAL CPU FIX CHECKS PASSED
```

---

## 4. Differential Oracle Results (Unicorn 2.1.4 vs WinIOS)

**Command:**
```powershell
python tests/tools/diff_cpu/diff_arm64_oracle.py --runner build/Release/diff_winios_runner.exe --seed 2026192 --report tests/tools/diff_cpu/diff_report_192.json
```

### Statistical Summary
| Metric | Value | Conformance Rate |
| :--- | :--- | :--- |
| **Total Test Cases** | **4,300** | 100.0% |
| **Successfully Compared** | **4,300** | 100.0% |
| **Exact Bit-for-Bit Matches** | **4,300** | **100.0%** |
| **Mismatched State** | **0** | **0.0%** |
| **WinIOS Unsupported Errors** | **0** | **0.0%** |
| **External Oracle Faults** | **0** | **0.0%** |

### Category Breakdown
| Category | Cases | Matches | Mismatches | Unsupported | Status |
| :--- | :---: | :---: | :---: | :---: | :---: |
| `int_alu` | 300 | 300 | 0 | 0 | **PASS** |
| `logic_bit` | 250 | 250 | 0 | 0 | **PASS** |
| `cond` | 150 | 150 | 0 | 0 | **PASS** |
| `branch` | 200 | 200 | 0 | 0 | **PASS** |
| `ldst` | 250 | 250 | 0 | 0 | **PASS** |
| `simd_fp` | 350 | 350 | 0 | 0 | **PASS** |
| `chains_imm` | 300 | 300 | 0 | 0 | **PASS** |
| `adc_sbc_stress` | 500 | 500 | 0 | 0 | **PASS** |
| `shifted_extended_arith_stress` | 500 | 500 | 0 | 0 | **PASS** |
| `cond_edge_cases` | 500 | 500 | 0 | 0 | **PASS** |
| `wreg_zero_extension_sp_xzr` | 500 | 500 | 0 | 0 | **PASS** |
| `simd_vector_corner_cases` | 500 | 500 | 0 | 0 | **PASS** |
| **TOTAL** | **4,300** | **4,300** | **0** | **0** | **100.0%** |

---

## 5. Evidence Classification

- **Category:** `DIFFERENTIAL` & `UNIT`
- **Integrity Guarantee:** Deterministic pseudorandom test vectors generated from seed `2026192`, fully validated against external Unicorn 2.1.4 oracle.
- **Artifact:** [`tests/tools/diff_cpu/diff_report_192.json`](file:///D:/WinISO26/WinIOS/tests/tools/diff_cpu/diff_report_192.json)
