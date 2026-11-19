# Stage 235.5 — Ed25519 Signing Timing Evidence & Side-Channel Resistance

## 1. Executive Summary
Stage 235.5 evaluated the empirical runtime variance of the hardened constant-time Ed25519 signing implementation under differing private key bit distributions. Prior to Stage 235.3, the variable-time implementation terminated early when the remaining scalar was zero and conditionally performed curve additions based on private scalar bits, introducing severe timing discrepancies between low-density and high-density keys.

Following the introduction of the complete Twisted Edwards addition and fixed 256-iteration double-and-add loop with branchless `point_cselect`, empirical measurement across four distinct key classes demonstrated timing invariance, with class mean spread below 1.5%.

## 2. Methodology & Test Setup
- **Binary:** `stage235_5_timing_evidence_tests`
- **Compiler:** MSVC C++20 (`/W4 /WX /permissive-`)
- **Configuration:** `RelWithDebInfo`
- **Measurement:** High-resolution hardware monotonic timer (`std::chrono::high_resolution_clock`).
- **Sample Size:** 30 complete signing operations per key class (120 total), preceded by cache warmup iterations.
- **Message Payload:** Constant 63-byte diagnostic payload.

## 3. Empirical Timing Distribution

| Key Class | Description | Mean (ms) | Median (ms) | StdDev (ms) | CV (%) |
|:---|:---|:---:|:---:|:---:|:---:|
| **Zero Seed (0x00)** | Clamped key with minimal non-clamped bits | 16.928 | 16.524 | 0.914 | 5.40% |
| **One Seed (0xFF)** | Clamped key with high Hamming weight | 16.678 | 16.518 | 0.416 | 2.50% |
| **Alt-5 Seed (0x55)** | Alternating bit pattern (`01010101...`) | 16.684 | 16.497 | 0.391 | 2.35% |
| **Alt-A Seed (0xAA)** | Alternating bit pattern (`10101010...`) | 16.850 | 16.579 | 0.664 | 3.94% |

### Statistical Metrics
- **Minimum Class Mean:** 16.678 ms (One Seed)
- **Maximum Class Mean:** 16.928 ms (Zero Seed)
- **Spread Across Class Means:** **1.50%**
- **Median Invariance:** Median runtimes across all four radically distinct key classes lie between 16.497 ms and 16.579 ms (a variation of less than 0.082 ms, or 0.49%).
- **Coefficient of Variation:** All key classes exhibit $CV < 6\%$, consistent with normal Windows thread scheduling and frequency jitter on non-isolated CPU cores.

## 4. Disassembly and Branch Inspection
Inspection of the compiled code for `point_scalarmult` reveals:
1. **Loop Structure:** Single loop compiled with a fixed counter incrementing from 0 to 255.
2. **Conditional Selection:** `point_cselect` compiles to XOR-AND-XOR logic with a mask derived from `0 - bit`. No conditional jump instructions (`je`, `jne`, `jz`, `jnz`) depend on secret scalar bits.
3. **Denominators & Inversion:** Montgomery batch inversion executes unconditionally on every doubling and addition. No identity point checks or zero-denominator checks exist in the loop body.

## 5. Verification Status
- Target: `stage235_5_timing_evidence_tests`
- RelWithDebInfo Execution: 6.35s (PASSED)
- Debug Execution: 64.31s (PASSED)
- Compiler Compliance: 0 warnings, 0 errors.
