# Stage 232.7 Deliverable — WIOSAUTH v2 Authenticated Format

## 1. Overview
Stage 232.7 implements the versioned trailer specification `WIOSAUTH v2` (`src/security/publisher_trust_store.h`, `src/security/publisher_trust_store.cpp`), resolving security finding **SEC-232-02** by cryptographically binding all trailer metadata to the digital signature domain.

## 2. Specification & Cryptographic Domain
1. **Trailer Layout (WIOSAUTH v2):**
   - Header Magic: `WIOSAUTH` (8 bytes)
   - Version: `2` (uint32_le, 4 bytes)
   - Algorithm: `1` = HMAC-SHA256, `2` = Ed25519 (uint32_le, 4 bytes)
   - Publisher ID: Length (uint16_le, 2 bytes) + UTF-8 string
   - Key ID: Length (uint16_le, 2 bytes) + UTF-8 string
   - Valid From: uint64_le (8 bytes)
   - Valid Until: uint64_le (8 bytes)
   - Header Flags: uint32_le (4 bytes)
   - Content Digest: SHA-256 canonical package digest (32 bytes)
   - Signature: Length (uint16_le, 2 bytes; 64 for Ed25519, 32 for HMAC) + signature bytes
   - Total Trailer Length: uint32_le (4 bytes, footer)
2. **Cryptographic Binding (Signature Domain):**
   - Domain string prefix: `WIOS-PKG-AUTH-V2:`
   - Signed input:
     $$\text{DomainInput} = \text{"WIOS-PKG-AUTH-V2:"} \parallel \text{ContentDigest}_{32} \parallel \text{Algorithm}_{4} \parallel \text{PublisherID} \parallel \text{KeyID} \parallel \text{ValidFrom}_{8} \parallel \text{ValidUntil}_{8} \parallel \text{Flags}_{4}$$
   - Any modification to expiration timestamps, algorithm, key ID, publisher ID, or flags invalidates the signature, preventing attacker metadata substitution.
3. **Backward Compatibility:**
   - `verify_package_signature()` dynamically handles both v1 and v2 trailers.
   - V1 signatures are parsed using the v1 domain prefix (`WIOS-PKG-AUTH-V1:`); V2 signatures verify against the full metadata-bound domain.
   - Default signing for new packages uses WIOSAUTH v2.

## 3. Test Verification
- Executed unit tests in `tests/unit/stage232_7_wiosauth_v2_tests.cpp` (registered as CTest #928).
- Tests passed:
  1. `test_wiosauth_v2_ed25519`: Successful generation and verification of v2 Ed25519 envelope, checking trailer version and flags.
  2. `test_wiosauth_v2_hmac`: Successful generation and verification of v2 HMAC-SHA256 envelope.
  3. `test_backward_compatibility_v1`: Successful verification of legacy v1 envelopes without regressions.
  4. `test_tamper_content_digest`: Modification of payload rejected immediately via content digest mismatch.
  5. `test_tamper_trailer_metadata_fails_signature`: Modification of trailer flags alone (leaving payload and content digest intact) causes cryptographic signature failure, verifying complete metadata binding.
  6. `test_unknown_version_rejected`: Unsupported trailer version cleanly rejected.
- Build Status: Zero warnings and zero errors under MSVC `/W4 /WX /permissive-` in both Debug and RelWithDebInfo.
