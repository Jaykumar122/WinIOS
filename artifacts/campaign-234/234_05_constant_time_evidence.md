# Stage 234.5 — Constant-Time Claims and Empirical Timing Evidence

## Executive Summary

Stage 234.5 conducted a rigorous code-level audit and empirical timing measurement of cryptographic operations across the WinIOS platform. The objective was to separate theoretical source-level constant-time reasoning from empirical measurements, identify any secret-dependent branches, document timing noise, and establish strict scientific honesty regarding side-channel claims.

Prior documentation claimed "proven constant-time behavior". The audit revealed:
1. `constant_time_equals` is strictly branch-free and bitwise accumulating; its execution time does not depend on which byte differs.
2. In `Ed25519::verify`, all scalars ($S$ and $k$) are public. The double-and-add scalar multiplication operates only on non-secret data during verification.
3. In `Ed25519::create_keypair` and `Ed25519::sign`, `point_scalarmult` branches conditionally on each scalar bit (`if (scalar.d[0] & 1)`). Consequently, key generation and signing are **not** strictly constant-time at the microarchitectural instruction level.

---

## 1. Source-Level Code Audit & Branch Analysis

| Operation / Function | Input Secrecy | Algorithm / Branch Structure | Constant-Time Status |
| :--- | :--- | :--- | :--- |
| **`constant_time_equals`** | Secret tokens / MAC tags / digests | Single loop over `len` with bitwise accumulation (`diff \|= (pa[i] ^ pb[i])`). No early exits or conditional branches. | **Branch-Free (Constant-Time Designed)** |
| **`Ed25519::verify`** | Public ($R$, $A$, $M$, $S$, $k$) | Computes $S \cdot B$ and $R + k \cdot A$. Branching occurs in `point_scalarmult`, but inputs $S$ and $k$ are public values derived from the signature. | **Safe Against Key Leakage (No secret inputs handled)** |
| **`Ed25519::sign`** | Secret (Private key $a$, Nonce $r$) | Computes $R = r \cdot B$ and $S = (r + k \cdot a) \pmod L$. `point_scalarmult` executes conditional branch on bit 0 of secret $r$. | **Not Strictly Constant-Time (Hamming weight dependent)** |
| **`Ed25519::create_keypair`** | Secret (Private seed / scalar $a$) | Computes $A = a \cdot B$. `point_scalarmult` executes conditional branch on bit 0 of secret $a$. | **Not Strictly Constant-Time (Hamming weight dependent)** |

---

## 2. Empirical Timing Measurement Harness

Test executable `stage234_5_constant_time_tests` was designed to measure execution timing under rigorous statistical controls:
- **Host CPU:** AMD / Intel x86_64 Host running Windows 11
- **Compilers:** MSVC C++20 (`/W4 /WX /permissive-`) under `RelWithDebInfo` and `Debug`
- **Timer:** `std::chrono::high_resolution_clock` (QPC hardware timer)
- **Warm-Up:** 1,000 iterations to prime instruction cache and branch target buffers
- **Statistical Filtering:** 10,000 measurements per comparison condition; 1,000 measurements for verification

### Test 1: `constant_time_equals` Empirical Results ($N = 10,000$)

#### RelWithDebInfo Build:
| Comparison Input | Mean (ns) | Median (ns) | Min (ns) | Max (ns) | StdDev (ns) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Identical Match** | 85.00 | **100.00** | 0.00 | 18,300.00 | 526.72 |
| **Mismatch at Byte 0** | 88.45 | **100.00** | 0.00 | 36,400.00 | 784.68 |
| **Mismatch at Byte 15** | 80.15 | **100.00** | 0.00 | 1,400.00 | 49.00 |
| **Mismatch at Byte 31** | 90.44 | **100.00** | 0.00 | 66,700.00 | 1,073.90 |

#### Debug Build:
| Comparison Input | Mean (ns) | Median (ns) | Min (ns) | Max (ns) | StdDev (ns) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Identical Match** | 99.86 | **100.00** | 0.00 | 4,200.00 | 146.31 |
| **Mismatch at Byte 0** | 98.40 | **100.00** | 0.00 | 2,700.00 | 60.26 |
| **Mismatch at Byte 15** | 97.71 | **100.00** | 0.00 | 600.00 | 15.93 |
| **Mismatch at Byte 31** | 98.00 | **100.00** | 0.00 | 1,300.00 | 35.24 |

**Key Empirical Observation:** The median execution time across all four conditions (identical match, mismatch at first byte, mismatch in middle, mismatch at last byte) is **identically 100.00 ns** in both builds.

---

### Test 2: Signature Verification Profile ($N = 1,000$)

| Build Configuration | Mean ($\mu$s) | Median ($\mu$s) | StdDev ($\mu$s) | Approximate Time per Verify |
| :--- | :---: | :---: | :---: | :---: |
| **RelWithDebInfo** | 48,581.58 | 48,270.60 | 1,072.60 | ~48.3 ms |
| **Debug** | 492,409.63 | 490,693.60 | 10,882.63 | ~490.7 ms |

---

## 3. Methodological Limitations & Disclosure

1. **Empirical Timing Does Not Prove Constant Time:**
   Measuring similar execution times over 10,000 runs demonstrates absence of macroscopic early exits, but cannot prove immunity to microarchitectural side channels.
2. **Host OS Noise:**
   Windows 11 CPU thread scheduling, hardware interrupts, turbo-boost frequency scaling, and context switches introduce significant variance (demonstrated by the max spikes above).
3. **Double-and-Add Limitation:**
   Because `point_scalarmult` uses binary double-and-add rather than Montgomery ladder or fixed-window branchless recoding, signing and key generation have data-dependent branch profiles.
