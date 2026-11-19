# Stage 234.3 — Ed25519 Negative and Malformed Input Testing Report

## Executive Summary

Stage 234.3 significantly expanded negative and adversarial test coverage for the Ed25519 signature implementation in `src/crypto/ed25519.cpp`. While RFC 8032 test vectors 1–4 confirm basic functionality on known positive inputs, robust security requires strict mathematical validation against out-of-range scalars, non-canonical curve encodings, points not on the curve, small-order degenerate points, and malformed inputs.

Test executable `stage234_3_ed25519_negative_tests` was implemented, integrated into CMake, and validated across both `RelWithDebInfo` and `Debug` configurations with a 100% pass rate.

---

## 1. Adversarial Test Vector Matrix

| Test Category | Injected Fault / Vector | Mathematical Condition | Expected Outcome | Observed Result | Status |
| :--- | :--- | :--- | :---: | :---: | :---: |
| **Public Key Encoding** | $y = p$ ($2^{255} - 19$) | Non-canonical point encoding ($y \ge p$) | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Public Key Encoding** | $y = p + 1$ ($2^{255} - 18$) | Non-canonical point encoding ($y \ge p$) | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Public Key Encoding** | $y = 2^{255} - 1$ | Non-canonical point encoding ($y \ge p$) | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Public Key Encoding** | $y = 2$ | Non-quadratic residue in $\mathbb{F}_p$ (Not on Curve25519) | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Signature $R$-Point** | $R.y = p$ | Non-canonical point encoding ($R.y \ge p$) | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Signature $R$-Point** | $R.y = 2^{255} - 1$ | Non-canonical point encoding ($R.y \ge p$) | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Signature $R$-Point** | $R.y = 2$ | Non-quadratic residue (Point not on curve) | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Signature Scalar $S$** | $S = 0$ | Out-of-bounds scalar ($S \notin (0, L)$) | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Signature Scalar $S$** | $S = L$ (Group order) | RFC 8032 Sec 5.1.7 violation ($S \ge L$) | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Signature Scalar $S$** | $S = L + 1$ | Non-canonical malleable scalar | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Signature Scalar $S$** | $S = 2^{256} - 1$ | Max 256-bit scalar ($S \ge L$) | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Small-Order Points** | Public Key = Identity $(0, 1)$ | Order-1 neutral group element | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Small-Order Points** | $R$-Point = Identity $(0, 1)$ | Order-1 neutral group element | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Small-Order Points** | Public Key = $(0, 0)$ | Degenerate point (Not on curve: $0 \ne 1$) | Reject (`false`) | Rejected (`false`) | **PASS** |
| **Boundary Messages** | Message length = 0 | Empty message string | Verify (`true`) | Verified (`true`) | **PASS** |
| **Boundary Messages** | Message length = 65,536 | 64 KB binary payload | Verify (`true`) | Verified (`true`) | **PASS** |
| **Adversarial Stress** | 100 Random Permutations | Pseudorandom signatures & keys ($N=100$) | Reject (`false`) | 100% Rejected (`false`) | **PASS** |

---

## 2. Invariants & Security Guarantees Established

1. **Signature Malleability Defense:**
   - In standard Ed25519, if an implementation does not enforce $S < L$, an attacker can compute $S' = S + L \pmod{2^{256}}$ to produce a second valid signature for the same message and public key.
   - The WinIOS verifier explicitly validates $0 < S < L$, making signature malleability mathematically impossible.
2. **Strict Point Decompression:**
   - `point_from_bytes` verifies that the 255-bit $y$-coordinate satisfies $y < 2^{255}-19$ and recovers $x$ via Fermat's Little Theorem only if $x^2 = (y^2 - 1)/(d y^2 + 1)$ is quadratic residue. Malformed or non-canonical encodings are immediately rejected.
3. **Small-Order Subgroup Attacks:**
   - The verifier rejects the identity element $(0, 1)$ for both public key $A$ and signature point $R$.
