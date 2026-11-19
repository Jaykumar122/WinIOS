# Stage 235.3 — Constant-Time Scalar Multiplication Implementation & Integration

## 1. Executive Summary
In Stage 235.3, the secret-dependent timing vulnerabilities in `src/crypto/ed25519.cpp` identified in Stage 235.1 and designed in Stage 235.2 were resolved. The variable-time scalar multiplication loop was replaced with a strictly constant-time, fixed 256-iteration double-and-add algorithm using bitwise conditional selection (`point_cselect`). Denominator inversions were halved via Montgomery simultaneous inversion, eliminating identity check branches. Furthermore, `f_add`, `f_sub`, `sc_reduce_512`, and `sc_muladd_L` were migrated to branchless conditional arithmetic.

## 2. Implementation Details

### 2.1 Branch-Free Field Arithmetic
- `f_add`:
  Computes `add_carry(r, a, b)` followed by trial subtraction `sub_borrow(tmp, r, P_MOD)`. The mask `mask = 0U - (1U - borrow)` conditionally selects `tmp` in constant time without branching.
- `f_sub`:
  Computes `sub_borrow(diff, a, b)` followed by `add_carry(with_p, diff, P_MOD)`. The mask `mask = 0U - borrow` conditionally selects `with_p` when underflow occurs, completely branchless.

### 2.2 Complete Edwards Point Addition & Simultaneous Inversion
- In Twisted Edwards coordinates for Curve25519 with $d = -121665/121666 \pmod p$, denominators $1 + dx_1x_2y_1y_2$ and $1 - dx_1x_2y_1y_2$ are strictly non-zero for all valid curve points.
- Identity checks (`if (P.is_identity) return Q;`) were eliminated. The algebraic formula maps $(0, 1) + Q \to Q$ and $P + (0, 1) \to P$ unconditionally.
- Montgomery batch inversion computes $inv_{xy} = (den_x \cdot den_y)^{-1}$, followed by $inv_x = inv_{xy} \cdot den_y$ and $inv_y = inv_{xy} \cdot den_x$, reducing field exponentiations from 2 to 1 per addition.

### 2.3 Constant-Time Scalar Multiplication
- `point_cselect`:
  Bitwise conditional multiplexer `res = a ^ (mask & (a ^ b))` where `mask = 0U - bit` (either `0x00000000` or `0xFFFFFFFF`).
- `point_scalarmult`:
  Executes an exact 256-iteration loop ($bit = 0 \dots 255$). Each iteration unconditionally computes `point_add(res, cur)`, conditionally updates `res` using `point_cselect`, and unconditionally doubles `cur = point_add(cur, cur)`.
- Replaces secret-dependent early loop exit (`while (!scalar.is_zero())`) and secret-dependent addition branching.

### 2.4 Modulo L Arithmetic Hardening
- `sc_reduce_512`:
  In each bit step, conditional subtraction of $L$ is performed via `sub_borrow(sub_res, rem, L_ORDER)` and bitwise mask selection `mask = 0U - (1U - borrow)`.
- `sc_muladd_L`:
  Conditional subtraction of $L$ after adding $r$ and $ka$ is performed via branchless mask selection.

## 3. Verification & Test Evidence
- **Test Suite:** `tests/unit/stage235_3_constant_time_scalar_tests.cpp` (Target `stage235_3_constant_time_scalar_tests`).
- **Tests Evaluated:**
  1. RFC 8032 Test Vector 1 (Empty message Known-Answer Test): PASSED.
  2. RFC 8032 Test Vector 2 (1-byte message Known-Answer Test): PASSED.
  3. Key Derivation & Signing Determinism across multi-iterations: PASSED.
  4. Negative Tampering Resistance (corrupted message, signature bit flip in R, signature bit flip in S, public key corruption): PASSED.
- **Regression Suite:**
  - `stage233_3_ed25519_conformance_tests`: PASSED (3.71s RelWithDebInfo).
  - `stage234_2_ed25519_cross_validation_tests`: PASSED (1.67s RelWithDebInfo).
  - `stage234_3_ed25519_negative_tests`: PASSED (0.38s RelWithDebInfo).
  - `stage235_3_constant_time_scalar_tests`: PASSED (0.81s RelWithDebInfo, 8.84s Debug).
- **Compiler Compliance:** MSVC C++20 `/W4 /WX /permissive-` (0 warnings, 0 errors).
