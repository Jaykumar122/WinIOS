#include "crypto/ed25519.h"
#include "crypto/sha512.h"
#include "security/security_utils.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace winios::crypto {

namespace {

// -----------------------------------------------------------------------------
// Unsigned 256-bit Big Integer (8 limbs of 32-bits, Little-Endian)
// -----------------------------------------------------------------------------
struct U256 {
    std::uint32_t d[8]{0};

    bool is_zero() const noexcept {
        for (std::uint32_t x : d) if (x != 0) return false;
        return true;
    }

    bool get_bit(std::size_t idx) const noexcept {
        if (idx >= 256) return false;
        return (d[idx / 32] >> (idx % 32)) & 1;
    }

    void set_bit(std::size_t idx, bool val) noexcept {
        if (idx >= 256) return;
        if (val) {
            d[idx / 32] |= (1U << (idx % 32));
        } else {
            d[idx / 32] &= ~(1U << (idx % 32));
        }
    }

    static int cmp(const U256& a, const U256& b) noexcept {
        for (int i = 7; i >= 0; --i) {
            if (a.d[i] < b.d[i]) return -1;
            if (a.d[i] > b.d[i]) return 1;
        }
        return 0;
    }

    bool operator==(const U256& o) const noexcept { return cmp(*this, o) == 0; }
    bool operator!=(const U256& o) const noexcept { return cmp(*this, o) != 0; }
    bool operator<(const U256& o) const noexcept { return cmp(*this, o) < 0; }
    bool operator>=(const U256& o) const noexcept { return cmp(*this, o) >= 0; }

    static U256 from_bytes_le(const std::uint8_t* b) noexcept {
        U256 r;
        for (int i = 0; i < 8; ++i) {
            r.d[i] = static_cast<std::uint32_t>(b[i * 4 + 0]) |
                    (static_cast<std::uint32_t>(b[i * 4 + 1]) << 8) |
                    (static_cast<std::uint32_t>(b[i * 4 + 2]) << 16) |
                    (static_cast<std::uint32_t>(b[i * 4 + 3]) << 24);
        }
        return r;
    }

    void to_bytes_le(std::uint8_t* b) const noexcept {
        for (int i = 0; i < 8; ++i) {
            b[i * 4 + 0] = static_cast<std::uint8_t>(d[i] & 0xFF);
            b[i * 4 + 1] = static_cast<std::uint8_t>((d[i] >> 8) & 0xFF);
            b[i * 4 + 2] = static_cast<std::uint8_t>((d[i] >> 16) & 0xFF);
            b[i * 4 + 3] = static_cast<std::uint8_t>((d[i] >> 24) & 0xFF);
        }
    }

    static std::uint32_t add_carry(U256& r, const U256& a, const U256& b) noexcept {
        std::uint64_t carry = 0;
        for (int i = 0; i < 8; ++i) {
            std::uint64_t sum = static_cast<std::uint64_t>(a.d[i]) + b.d[i] + carry;
            r.d[i] = static_cast<std::uint32_t>(sum & 0xFFFFFFFFULL);
            carry = sum >> 32;
        }
        return static_cast<std::uint32_t>(carry);
    }

    static std::uint32_t sub_borrow(U256& r, const U256& a, const U256& b) noexcept {
        std::int64_t borrow = 0;
        for (int i = 0; i < 8; ++i) {
            std::int64_t diff = static_cast<std::int64_t>(a.d[i]) - b.d[i] - borrow;
            if (diff < 0) {
                diff += 0x100000000ULL;
                borrow = 1;
            } else {
                borrow = 0;
            }
            r.d[i] = static_cast<std::uint32_t>(diff & 0xFFFFFFFFULL);
        }
        return static_cast<std::uint32_t>(borrow);
    }
};

struct U512 {
    std::uint32_t d[16]{0};

    static U512 mul(const U256& a, const U256& b) noexcept {
        U512 r;
        for (int i = 0; i < 8; ++i) {
            std::uint64_t carry = 0;
            for (int j = 0; j < 8; ++j) {
                std::uint64_t prod = static_cast<std::uint64_t>(a.d[i]) * b.d[j] + r.d[i + j] + carry;
                r.d[i + j] = static_cast<std::uint32_t>(prod & 0xFFFFFFFFULL);
                carry = prod >> 32;
            }
            r.d[i + 8] = static_cast<std::uint32_t>(carry);
        }
        return r;
    }

    static U512 mul_u32(const U256& a, std::uint32_t b) noexcept {
        U512 r;
        std::uint64_t carry = 0;
        for (int i = 0; i < 8; ++i) {
            std::uint64_t prod = static_cast<std::uint64_t>(a.d[i]) * b + carry;
            r.d[i] = static_cast<std::uint32_t>(prod & 0xFFFFFFFFULL);
            carry = prod >> 32;
        }
        r.d[8] = static_cast<std::uint32_t>(carry);
        return r;
    }
};

// Modulus p = 2^255 - 19
const U256 P_MOD = []() {
    U256 p;
    p.d[0] = 0xFFFFFFED;
    for (int i = 1; i < 7; ++i) p.d[i] = 0xFFFFFFFF;
    p.d[7] = 0x7FFFFFFF;
    return p;
}();

// Edwards parameter d = -121665 / 121666 mod p
// 0x52036cee2b6ffe738cc740797779e89800700a4d4141d8ab75eb4dca135978a3
const U256 D_PARAM = []() {
    std::uint8_t d_bytes[32] = {
        0xa3, 0x78, 0x59, 0x13, 0xca, 0x4d, 0xeb, 0x75,
        0xab, 0xd8, 0x41, 0x41, 0x4d, 0x0a, 0x70, 0x00,
        0x98, 0xe8, 0x79, 0x77, 0x79, 0x40, 0xc7, 0x8c,
        0x73, 0xfe, 0x6f, 0x2b, 0xee, 0x6c, 0x03, 0x52
    };
    return U256::from_bytes_le(d_bytes);
}();

// Modulus L = 2^252 + 27742317777372353535851937790883648493
// 0x1000000000000000000000000000000014def9dea2f79cd65812631a5cf5d3ed
const U256 L_ORDER = []() {
    std::uint8_t l_bytes[32] = {
        0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
    };
    return U256::from_bytes_le(l_bytes);
}();

// -----------------------------------------------------------------------------
// Finite Field F_p Arithmetic (mod p = 2^255 - 19)
// -----------------------------------------------------------------------------

U256 f_reduce(const U512& x) noexcept {
    // x = H * 2^256 + L == 38 * H + L (mod p)
    U256 L_part, H_part;
    std::memcpy(L_part.d, x.d, 32);
    std::memcpy(H_part.d, x.d + 8, 32);

    U512 h38 = U512::mul_u32(H_part, 38);
    U256 h38_lo;
    std::memcpy(h38_lo.d, h38.d, 32);
    std::uint32_t h38_hi = h38.d[8];

    U256 r1;
    std::uint32_t carry1 = U256::add_carry(r1, L_part, h38_lo);
    std::uint64_t overflow = carry1 + h38_hi;

    // Second reduction for overflow * 2^256
    U512 ov38 = U512::mul_u32(r1, 0); // zero init
    (void)ov38;
    U256 r2 = r1;
    if (overflow > 0) {
        U256 ov_add;
        ov_add.d[0] = static_cast<std::uint32_t>(overflow * 38);
        U256::add_carry(r2, r1, ov_add);
    }

    // High bit reduction (bit 255: 2^255 == 19)
    if (r2.d[7] & 0x80000000U) {
        r2.d[7] &= 0x7FFFFFFFU;
        U256 add19;
        add19.d[0] = 19;
        U256::add_carry(r2, r2, add19);
    }

    while (r2 >= P_MOD) {
        U256::sub_borrow(r2, r2, P_MOD);
    }
    return r2;
}

U256 f_add(const U256& a, const U256& b) noexcept {
    U256 r;
    U256::add_carry(r, a, b);
    U256 tmp;
    std::uint32_t borrow = U256::sub_borrow(tmp, r, P_MOD);
    std::uint32_t mask = 0U - (1U - borrow);
    for (int i = 0; i < 8; ++i) {
        r.d[i] = r.d[i] ^ (mask & (r.d[i] ^ tmp.d[i]));
    }
    return r;
}

U256 f_sub(const U256& a, const U256& b) noexcept {
    U256 diff;
    std::uint32_t borrow = U256::sub_borrow(diff, a, b);
    U256 with_p;
    U256::add_carry(with_p, diff, P_MOD);
    std::uint32_t mask = 0U - borrow;
    U256 r;
    for (int i = 0; i < 8; ++i) {
        r.d[i] = diff.d[i] ^ (mask & (diff.d[i] ^ with_p.d[i]));
    }
    return r;
}

U256 f_mul(const U256& a, const U256& b) noexcept {
    U512 p = U512::mul(a, b);
    return f_reduce(p);
}

U256 f_pow(U256 base, U256 exp) noexcept {
    U256 res;
    res.d[0] = 1;
    while (!exp.is_zero()) {
        if (exp.d[0] & 1) {
            res = f_mul(res, base);
        }
        base = f_mul(base, base);
        // Shift exp right by 1
        std::uint32_t carry = 0;
        for (int i = 7; i >= 0; --i) {
            std::uint32_t next_carry = exp.d[i] & 1;
            exp.d[i] = (exp.d[i] >> 1) | (carry << 31);
            carry = next_carry;
        }
    }
    return res;
}

U256 f_inv(const U256& a) noexcept {
    // Fermat's Little Theorem: a^(p - 2) mod p
    U256 p_minus_2;
    U256 two;
    two.d[0] = 2;
    U256::sub_borrow(p_minus_2, P_MOD, two);
    return f_pow(a, p_minus_2);
}

// -----------------------------------------------------------------------------
// Edwards Curve Point Operations
// -----------------------------------------------------------------------------
struct Point {
    U256 x;
    U256 y;
    bool is_identity{false};

    static Point identity() noexcept {
        Point pt;
        pt.x = U256{};
        pt.y.d[0] = 1;
        pt.is_identity = true;
        return pt;
    }
};

// Base point B: y = 4/5 mod p, x = xrecover(y)
// x = 0x216936d3cd6e53fec0a4e231fdd6dc5c692cc7609525a7b2c9562d608f25d51a
// y = 0x6666666666666666666666666666666666666666666666666666666666666658
const Point BASE_POINT = []() {
    Point b;
    std::uint8_t bx[32] = {
        0x1a, 0xd5, 0x25, 0x8f, 0x60, 0x2d, 0x56, 0xc9,
        0xb2, 0xa7, 0x25, 0x95, 0x60, 0xc7, 0x2c, 0x69,
        0x5c, 0xdc, 0xd6, 0xfd, 0x31, 0xe2, 0xa4, 0xc0,
        0xfe, 0x53, 0x6e, 0xcd, 0xd3, 0x36, 0x69, 0x21
    };
    std::uint8_t by[32] = {
        0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
        0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66
    };
    b.x = U256::from_bytes_le(bx);
    b.y = U256::from_bytes_le(by);
    b.is_identity = false;
    return b;
}();

// Constant-time point conditional select
// mask must be 0x00000000 or 0xFFFFFFFF
inline Point point_cselect(const Point& a, const Point& b, std::uint32_t mask) noexcept {
    Point res;
    for (int i = 0; i < 8; ++i) {
        res.x.d[i] = a.x.d[i] ^ (mask & (a.x.d[i] ^ b.x.d[i]));
        res.y.d[i] = a.y.d[i] ^ (mask & (a.y.d[i] ^ b.y.d[i]));
    }
    res.is_identity = false;
    return res;
}

Point point_add(const Point& P, const Point& Q) noexcept {
    // Complete Twisted Edwards addition on Curve25519:
    // x3 = (x1*y2 + x2*y1) / (1 + d*x1*x2*y1*y2)
    // y3 = (y1*y2 + x1*x2) / (1 - d*x1*x2*y1*y2)
    // Since d is non-square in F_p, denominators are strictly non-zero for all curve points.
    // Addition is algebraically complete: handles identity and doubling without branching.
    U256 x1y2 = f_mul(P.x, Q.y);
    U256 x2y1 = f_mul(Q.x, P.y);
    U256 x1x2 = f_mul(P.x, Q.x);
    U256 y1y2 = f_mul(P.y, Q.y);

    U256 dxxyy = f_mul(D_PARAM, f_mul(x1x2, y1y2));

    U256 num_x = f_add(x1y2, x2y1);
    U256 one; one.d[0] = 1;
    U256 den_x = f_add(one, dxxyy);

    U256 num_y = f_add(y1y2, x1x2);
    U256 den_y = f_sub(one, dxxyy);

    // Constant-time simultaneous inversion (Montgomery batch inversion)
    U256 den_xy = f_mul(den_x, den_y);
    U256 inv_xy = f_inv(den_xy);
    U256 inv_x = f_mul(inv_xy, den_y);
    U256 inv_y = f_mul(inv_xy, den_x);

    Point r;
    r.x = f_mul(num_x, inv_x);
    r.y = f_mul(num_y, inv_y);
    r.is_identity = false;
    return r;
}

Point point_scalarmult(Point P, U256 scalar) noexcept {
    // Constant-time fixed-256-iteration double-and-add scalar multiplication.
    // Eliminates secret-dependent early termination, secret-dependent branch selection,
    // and non-uniform instruction sequences.
    Point res = Point::identity();
    Point cur = P;

    for (int bit_idx = 0; bit_idx < 256; ++bit_idx) {
        std::uint32_t word = scalar.d[bit_idx / 32];
        std::uint32_t bit = (word >> (bit_idx % 32)) & 1U;
        std::uint32_t mask = 0U - bit;

        Point added = point_add(res, cur);
        res = point_cselect(res, added, mask);
        cur = point_add(cur, cur);
    }
    return res;
}

// Recover x from y: x^2 = (y^2 - 1) / (d*y^2 + 1)
bool point_x_recover(U256& out_x, const U256& y) noexcept {
    U256 y2 = f_mul(y, y);
    U256 one; one.d[0] = 1;
    U256 u = f_sub(y2, one);
    U256 v = f_add(f_mul(D_PARAM, y2), one);
    U256 xx = f_mul(u, f_inv(v));

    // x = xx^((p+3)/8) mod p
    // (p+3)/8 = 2^252 - 2
    U256 exp;
    exp.d[0] = 0xFFFFFFFE;
    for (int i = 1; i < 7; ++i) exp.d[i] = 0xFFFFFFFF;
    exp.d[7] = 0x0FFFFFFF;

    U256 x = f_pow(xx, exp);
    if (f_mul(x, x) != xx) {
        // x = x * 2^((p-1)/4) mod p
        // 2^((p-1)/4) == sqrt(-1)
        std::uint8_t sqrtm1_bytes[32] = {
            0xb0, 0xa0, 0x0e, 0x4a, 0x27, 0x1b, 0xee, 0xc4,
            0x78, 0xe4, 0x2f, 0xad, 0x06, 0x18, 0x43, 0x2f,
            0xa7, 0xd7, 0xfb, 0x3d, 0x99, 0x00, 0x4d, 0x2b,
            0x0b, 0xdf, 0xc1, 0x4f, 0x80, 0x24, 0x83, 0x2b
        };
        U256 sqrtm1 = U256::from_bytes_le(sqrtm1_bytes);
        x = f_mul(x, sqrtm1);
        if (f_mul(x, x) != xx) {
            return false;
        }
    }
    out_x = x;
    return true;
}

void point_to_bytes(std::uint8_t out[32], const Point& P) noexcept {
    P.y.to_bytes_le(out);
    if (P.x.d[0] & 1) {
        out[31] |= 0x80;
    }
}

bool point_from_bytes(Point& out_p, const std::uint8_t in[32]) noexcept {
    U256 y = U256::from_bytes_le(in);
    bool x_bit = (in[31] & 0x80) != 0;
    y.d[7] &= 0x7FFFFFFFU;

    // RFC 8032 Section 5.1.3: If y >= p, decoding fails
    if (y >= P_MOD) {
        return false;
    }

    U256 x;
    if (!point_x_recover(x, y)) {
        return false;
    }
    if ((x.d[0] & 1) != static_cast<std::uint32_t>(x_bit ? 1 : 0)) {
        x = f_sub(P_MOD, x);
    }
    out_p.x = x;
    out_p.y = y;
    out_p.is_identity = false;
    return true;
}

// -----------------------------------------------------------------------------
// Modulo L Arithmetic
// -----------------------------------------------------------------------------

// Reduce 512-bit integer modulo L using bit shift-and-subtract
U256 sc_reduce_512(const std::uint8_t in[64]) noexcept {
    // 512-bit accumulator
    std::uint32_t acc[16];
    for (int i = 0; i < 16; ++i) {
        acc[i] = static_cast<std::uint32_t>(in[i * 4 + 0]) |
                (static_cast<std::uint32_t>(in[i * 4 + 1]) << 8) |
                (static_cast<std::uint32_t>(in[i * 4 + 2]) << 16) |
                (static_cast<std::uint32_t>(in[i * 4 + 3]) << 24);
    }

    U256 rem{};
    for (int i = 511; i >= 0; --i) {
        // Shift rem left by 1
        std::uint32_t carry = 0;
        for (int j = 0; j < 8; ++j) {
            std::uint32_t next = (rem.d[j] >> 31) & 1;
            rem.d[j] = (rem.d[j] << 1) | carry;
            carry = next;
        }
        // Insert bit i
        std::uint32_t bit = (acc[i / 32] >> (i % 32)) & 1U;
        rem.d[0] |= bit;

        // Constant-time conditional subtraction of L_ORDER
        U256 sub_res;
        std::uint32_t borrow = U256::sub_borrow(sub_res, rem, L_ORDER);
        std::uint32_t mask = 0U - (1U - borrow);
        for (int j = 0; j < 8; ++j) {
            rem.d[j] = rem.d[j] ^ (mask & (rem.d[j] ^ sub_res.d[j]));
        }
    }
    return rem;
}

// S = (r + k * a) mod L
U256 sc_muladd_L(const U256& r, const U256& k, const U256& a) noexcept {
    U512 prod = U512::mul(k, a);
    std::uint8_t prod_bytes[64];
    for (int i = 0; i < 16; ++i) {
        prod_bytes[i * 4 + 0] = static_cast<std::uint8_t>(prod.d[i] & 0xFF);
        prod_bytes[i * 4 + 1] = static_cast<std::uint8_t>((prod.d[i] >> 8) & 0xFF);
        prod_bytes[i * 4 + 2] = static_cast<std::uint8_t>((prod.d[i] >> 16) & 0xFF);
        prod_bytes[i * 4 + 3] = static_cast<std::uint8_t>((prod.d[i] >> 24) & 0xFF);
    }
    U256 ka = sc_reduce_512(prod_bytes);

    // Add r: r < L and ka < L, so res = r + ka < 2L < 2^254 < 2^256.
    U256 res;
    U256::add_carry(res, r, ka);

    // Constant-time conditional subtraction of L_ORDER
    U256 sub_res;
    std::uint32_t borrow = U256::sub_borrow(sub_res, res, L_ORDER);
    std::uint32_t mask = 0U - (1U - borrow);
    for (int j = 0; j < 8; ++j) {
        res.d[j] = res.d[j] ^ (mask & (res.d[j] ^ sub_res.d[j]));
    }
    return res;
}

} // namespace

void Ed25519::create_keypair(Ed25519PublicKey& out_pub,
                             Ed25519PrivateKey& out_priv,
                             const Ed25519Seed& seed) noexcept {
    std::uint8_t h[64];
    Sha512::hash(seed.data(), seed.size(), h);

    // RFC 8032 clamping
    h[0] &= 248;
    h[31] &= 63;
    h[31] |= 64;

    U256 a = U256::from_bytes_le(h);
    Point A = point_scalarmult(BASE_POINT, a);
    point_to_bytes(out_pub.data(), A);

    // Private key is 32-byte seed + 32-byte public key
    std::memcpy(out_priv.data(), seed.data(), 32);
    std::memcpy(out_priv.data() + 32, out_pub.data(), 32);

    // Stage 233.4: Secure memory zeroization of private intermediates
    security::secure_zero_memory(h, sizeof(h));
    security::secure_zero_memory(&a, sizeof(a));
}

bool Ed25519::generate_keypair(Ed25519PublicKey& out_pub,
                               Ed25519PrivateKey& out_priv) noexcept {
    Ed25519Seed seed{};
    if (!security::SecureRandom::get_random_bytes(seed.data(), seed.size())) {
        return false;
    }
    create_keypair(out_pub, out_priv, seed);
    security::secure_zero_memory(seed.data(), seed.size());
    return true;
}

void Ed25519::sign(Ed25519Signature& out_sig,
                   const void* message,
                   std::size_t message_len,
                   const Ed25519PublicKey& public_key,
                   const Ed25519PrivateKey& private_key) noexcept {
    std::uint8_t h[64];
    Sha512::hash(private_key.data(), 32, h);

    h[0] &= 248;
    h[31] &= 63;
    h[31] |= 64;
    U256 a = U256::from_bytes_le(h);

    // 1. r = SHA-512(h[32..63] || message) mod L
    Sha512 sha_r;
    sha_r.update(h + 32, 32);
    sha_r.update(message, message_len);
    std::uint8_t r_hash[64];
    sha_r.finalize(r_hash);
    U256 r = sc_reduce_512(r_hash);

    // 2. R = r * B
    Point R = point_scalarmult(BASE_POINT, r);
    point_to_bytes(out_sig.data(), R);

    // 3. k = SHA-512(R || A || message) mod L
    Sha512 sha_k;
    sha_k.update(out_sig.data(), 32);
    sha_k.update(public_key.data(), 32);
    sha_k.update(message, message_len);
    std::uint8_t k_hash[64];
    sha_k.finalize(k_hash);
    U256 k = sc_reduce_512(k_hash);

    // 4. S = (r + k * a) mod L
    U256 S = sc_muladd_L(r, k, a);
    S.to_bytes_le(out_sig.data() + 32);

    // Stage 233.4: Secure memory zeroization of private stack intermediates
    security::secure_zero_memory(h, sizeof(h));
    security::secure_zero_memory(r_hash, sizeof(r_hash));
    security::secure_zero_memory(k_hash, sizeof(k_hash));
    security::secure_zero_memory(&a, sizeof(a));
    security::secure_zero_memory(&r, sizeof(r));
}

bool Ed25519::verify(const Ed25519Signature& signature,
                     const void* message,
                     std::size_t message_len,
                     const Ed25519PublicKey& public_key) noexcept {
    // 1. Check 0 < S < L (RFC 8032 Section 5.1.7)
    U256 S = U256::from_bytes_le(signature.data() + 32);
    if (S.is_zero() || S >= L_ORDER) {
        return false;
    }

    // 2. Parse R and A
    Point R, A;
    if (!point_from_bytes(R, signature.data())) {
        return false;
    }
    if (!point_from_bytes(A, public_key.data())) {
        return false;
    }

    // Reject small-order / degenerate points (identity point)
    if (A.is_identity || (A.x.is_zero() && A.y.d[0] == 1 && A.y.d[1] == 0 && A.y.d[2] == 0 && A.y.d[3] == 0 &&
                          A.y.d[4] == 0 && A.y.d[5] == 0 && A.y.d[6] == 0 && A.y.d[7] == 0)) {
        return false;
    }
    if (R.is_identity || (R.x.is_zero() && R.y.d[0] == 1 && R.y.d[1] == 0 && R.y.d[2] == 0 && R.y.d[3] == 0 &&
                          R.y.d[4] == 0 && R.y.d[5] == 0 && R.y.d[6] == 0 && R.y.d[7] == 0)) {
        return false;
    }

    // 3. k = SHA-512(R || A || message) mod L
    Sha512 sha_k;
    sha_k.update(signature.data(), 32);
    sha_k.update(public_key.data(), 32);
    sha_k.update(message, message_len);
    std::uint8_t k_hash[64];
    sha_k.finalize(k_hash);
    U256 k = sc_reduce_512(k_hash);

    // 4. Verify S * B == R + k * A
    Point SB = point_scalarmult(BASE_POINT, S);
    Point kA = point_scalarmult(A, k);
    Point R_plus_kA = point_add(R, kA);

    return (SB.x == R_plus_kA.x) && (SB.y == R_plus_kA.y);
}

std::string Ed25519::to_hex(const std::uint8_t* data, std::size_t len) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return oss.str();
}

bool Ed25519::from_hex(const std::string& hex, std::uint8_t* out_data, std::size_t expected_len) {
    if (hex.length() != expected_len * 2) return false;
    for (std::size_t i = 0; i < expected_len; ++i) {
        std::string byte_str = hex.substr(i * 2, 2);
        char* end = nullptr;
        unsigned long val = std::strtoul(byte_str.c_str(), &end, 16);
        if (!end || *end != '\0') return false;
        out_data[i] = static_cast<std::uint8_t>(val);
    }
    return true;
}

} // namespace winios::crypto
