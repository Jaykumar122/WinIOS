# Stage 235.2 — Constant-Time Scalar Multiplication Design

**Date:** 2026-11-20  
**Campaign:** 235.2  
**Subsystem:** Cryptography / Ed25519 / Constant-Time Curve Arithmetic  
**Status:** Complete  

---

## 1. Design Objectives & RFC 8032 Conformance

The objective of Stage 235.2 is to replace the secret-dependent double-and-add algorithm in `src/crypto/ed25519.cpp` with a provably branch-free, constant-time scalar multiplication implementation for private keys and ephemeral nonces, while preserving:
1. Exact bit-for-bit RFC 8032 signature generation.
2. Complete interoperability with existing public keys and verification vectors.
3. Zero external dependencies beyond standard C++20 and Windows CNG CSPRNG.
4. Strict compilation under MSVC `/W4 /WX /permissive-`.

---

## 2. Theoretical Foundation: Completeness of Twisted Edwards Addition

Curve25519 in Edwards form is defined by:
$$-x^2 + y^2 = 1 + d x^2 y^2 \pmod p$$
where $p = 2^{255} - 19$ and $d = -121665 / 121666 \pmod p$.

Because $d$ is a quadratic non-residue (non-square) in $\mathbb{F}_p$, the Edwards addition formulas are **strictly complete**:
$$x_3 = \frac{x_1 y_2 + x_2 y_1}{1 + d x_1 x_2 y_1 y_2} \pmod p$$
$$y_3 = \frac{y_1 y_2 + x_1 x_2}{1 - d x_1 x_2 y_1 y_2} \pmod p$$

### Mathematical Invariants:
1. **No Exceptional Cases:** The denominators $1 \pm d x_1 x_2 y_1 y_2$ are non-zero for all curve points $(x_1, y_1)$ and $(x_2, y_2)$, including identical points ($P = Q$) and the neutral identity element $\mathcal{O} = (0, 1)$.
2. **Elimination of Identity Branching:** The identity checks `if (P.is_identity) return Q;` can be safely removed because substituting $(0, 1)$ into the formula algebraically yields $(x_2, y_2)$ directly:
   $$x_3 = \frac{0 \cdot y_2 + x_2 \cdot 1}{1 + 0} = x_2, \quad y_3 = \frac{1 \cdot y_2 + 0 \cdot x_2}{1 - 0} = y_2$$

---

## 3. Algorithm Architecture: Fixed-Iteration Double-and-Add with Constant-Time Selection

Rather than branching conditionally on scalar bits, we employ a constant-time accumulator pattern:

```text
Input: Base point P, 256-bit secret scalar S
Output: Q = S * P

1. Initialize Accumulator: ACC = (0, 1) [Identity]
2. Initialize Base:        CUR = P
3. For bit_idx from 0 to 255:
     a. T = point_add(ACC, CUR)            // Unconditional addition
     b. bit = (S.d[bit_idx / 32] >> (bit_idx % 32)) & 1
     c. mask = (uint32_t)(0 - bit)         // 0x00000000 if bit == 0, 0xFFFFFFFF if bit == 1
     d. ACC = point_cselect(ACC, T, mask)  // Constant-time bitwise selection
     e. CUR = point_add(CUR, CUR)          // Unconditional doubling
4. Return ACC
```

### Constant-Time Selection Primitive (`point_cselect`)

The multiplexer `point_cselect(A, B, mask)` computes:
$$\text{out}_i = A_i \oplus (\text{mask} \ \& \ (A_i \oplus B_i))$$
- If `mask == 0x00000000`, $\text{out}_i = A_i$.
- If `mask == 0xFFFFFFFF`, $\text{out}_i = B_i$.
This compiles to branch-free bitwise operations or `cmov` instructions with zero control flow divergence.

```cpp
static Point point_cselect(const Point& a, const Point& b, std::uint32_t mask) noexcept {
    Point r;
    for (int i = 0; i < 8; ++i) {
        r.x.d[i] = a.x.d[i] ^ (mask & (a.x.d[i] ^ b.x.d[i]));
        r.y.d[i] = a.y.d[i] ^ (mask & (a.y.d[i] ^ b.y.d[i]));
    }
    r.is_identity = false;
    return r;
}
```

---

## 4. Constant-Time Field Arithmetic Hardening

To complement curve operations, field arithmetic primitives are updated to use branch-free conditional subtractions:

### 1. Constant-Time Modular Subtraction (`f_sub_ct`)
```cpp
U256 f_sub_ct(const U256& a, const U256& b) noexcept {
    U256 diff;
    std::uint32_t borrow = U256::sub_borrow(diff, a, b);
    // If borrow == 1 (a < b), add P_MOD
    std::uint32_t mask = static_cast<std::uint32_t>(0 - borrow);
    U256 p_masked;
    for (int i = 0; i < 8; ++i) p_masked.d[i] = P_MOD.d[i] & mask;
    U256 res;
    U256::add_carry(res, diff, p_masked);
    return res;
}
```

### 2. Constant-Time Modular Addition (`f_add_ct`)
```cpp
U256 f_add_ct(const U256& a, const U256& b) noexcept {
    U256 sum;
    std::uint32_t carry = U256::add_carry(sum, a, b);
    U256 diff;
    std::uint32_t borrow = U256::sub_borrow(diff, sum, P_MOD);
    // If carry == 1 or borrow == 0 (sum >= P_MOD), select diff; else select sum
    std::uint32_t use_diff = carry | (1U ^ borrow);
    std::uint32_t mask = static_cast<std::uint32_t>(0 - use_diff);
    U256 res;
    for (int i = 0; i < 8; ++i) {
        res.d[i] = sum.d[i] ^ (mask & (sum.d[i] ^ diff.d[i]));
    }
    return res;
}
```

---

## 5. Security & Side-Channel Assessment

1. **Loop Bound Invariance:** Exactly 256 iterations are executed for every secret scalar, regardless of leading or trailing zeros.
2. **Instruction Trace Parity:** In every iteration, exactly one point addition, one point doubling, one bit extraction, one bitwise mask expansion, and one conditional selection are executed.
3. **Data-Independent Memory Access:** Memory is accessed via linear sequential array iterations with static offsets (`i = 0..7`).
4. **RFC 8032 Output Invariance:** Mathematically identical outputs are produced for all inputs.
