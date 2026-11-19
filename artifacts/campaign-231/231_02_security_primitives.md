# Campaign 231 — Stage 231.2: Unified Security Primitives

## Objective
Establish a hardened, unified security primitives library (`winios::security`) providing:
1. Cryptographically secure pseudo-random number generation (CSPRNG) backed by Windows CNG `BCryptGenRandom` (`BCRYPT_USE_SYSTEM_PREFERRED_RNG`).
2. Constant-time equality comparison (`constant_time_equals`) across byte buffers, strings, and vectors to prevent timing side-channel attacks on digests, tokens, and cryptographic signatures.
3. Secure memory wiping (`secure_zero_memory`) utilizing `SecureZeroMemory` on Windows and volatile pointer zeroing to prevent compiler optimization dead-store elimination.
4. RAII `SecureBuffer` container ensuring immediate zeroing on destruction, move, or clearance, with deleted copy semantics to prevent secret duplication.
5. Safe integer arithmetic (`checked_add`, `checked_sub`, `checked_mul`) with template support for unsigned and signed integers, preventing numeric wraparounds in package/binary offset calculations.
6. Bounds-checked `BinaryReader` preventing buffer overreads during binary package or Mach-O decoding.

## Implementation Details
- **Files Created/Modified:**
  - `src/security/security_utils.h`: Public API declarations, safe arithmetic template helpers, and `BinaryReader`. Windows headers are cleanly isolated from the public header.
  - `src/security/security_utils.cpp`: Implementation of `SecureRandom`, `constant_time_equals`, `secure_zero_memory`, `SecureBuffer`, and `BinaryReader` binary operations. Links `bcrypt.lib`.
  - `CMakeLists.txt`: Added `src/security/security_utils.cpp` to `winios_core` and linked `bcrypt` on Windows.
  - `tests/unit/stage231_2_security_primitives_tests.cpp`: Comprehensive unit tests.

## Verification & Test Results
- Test Target: `stage231_2_security_primitives_tests` (Test #905)
- Configurations Tested:
  - `RelWithDebInfo`: Passed (0.05 sec)
  - `Debug`: Passed (0.05 sec)
- Test Coverage:
  - `test_csprng_randomness`: Null pointer edge cases, 1,000 unique 64-bit values, 1,000 unique 32-bit values, random hex strings, statistical distribution over 10,000 bytes.
  - `test_constant_time_equals`: Pointer validation, identical buffers, mismatches at index 0, midpoint, and last byte, string and vector overloads.
  - `test_secure_buffer_and_zeroing`: Memory zeroing verification, RAII cleanup, move construction, move assignment, dynamic resize.
  - `test_checked_arithmetic`: Overflow, underflow, and valid arithmetic for unsigned/signed 32-bit and 64-bit integers.
  - `test_binary_reader`: Endian-aware reads (u8, u16 LE, u32 LE, u64 LE, strings), bounds checking, out-of-bounds slice rejection, and span conversions.
