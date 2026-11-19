# Campaign 231 — Stage 231.4: Publisher Trust Store and Signature Verification

## Objective
Implement an in-memory publisher trust store and cryptographic signature verification subsystem (`winios::security::PublisherTrustStore`) supporting:
1. RFC 2104 compliant HMAC-SHA256 authenticated package envelopes.
2. Publisher key registration, deterministic key fixtures, and revocation management (by key ID or publisher ID).
3. In-place envelope signing (`PublisherTrustStore::sign_package`) with automatic domain-separated authenticated bytes calculation.
4. Comprehensive signature evaluation (`PublisherTrustStore::verify_package_signature`) returning discrete statuses: `Unsigned`, `DeveloperSigned`, `PlatformSigned`, `RevokedPublisher`, `CorruptSignature`, and `ExpiredSignature`.
5. Trust policy enforcement across Development Mode (permissive of registered developer keys) and Strict Production Mode (requiring platform root keys).

## Implementation Details
- **Files Created/Modified:**
  - `src/security/publisher_trust_store.h`: Trust store definitions, key structures, verification result records, and RFC 2104 `hmac_sha256` prototype.
  - `src/security/publisher_trust_store.cpp`: Implementation of RFC 2104 HMAC-SHA256, trailer block packing and unpacking, key revocation tracking, serialization/deserialization, and constant-time signature verification.
  - `CMakeLists.txt`: Added `publisher_trust_store.cpp` to `winios_core` and registered unit test target `stage231_4_trust_store_tests`.
  - `tests/unit/stage231_4_trust_store_tests.cpp`: Unit test suite covering all security verification cases.

## Verification & Test Results
- Test Target: `stage231_4_trust_store_tests` (Test #906)
- Configurations Tested:
  - `RelWithDebInfo`: Passed (0.05 sec)
  - `Debug`: Passed (0.06 sec)
- Test Coverage:
  - `test_hmac_rfc4231_vector`: Bit-for-bit verification against RFC 4231 Test Vector 1.
  - `test_platform_signed_verification`: Verified trusted in both Development and StrictProduction modes.
  - `test_developer_signed_verification`: Verified trusted in Development mode; strictly rejected in StrictProduction mode.
  - `test_unsigned_package`: Verified trusted in Development mode; rejected in StrictProduction mode.
  - `test_revoked_key_and_publisher`: Static revocation fixture and dynamic revocation via `revoke_key` and `revoke_publisher` tested and verified rejected.
  - `test_tampered_payload_and_corrupt_signature`: Single-byte payload tampering and signature trailer corruption tested and verified rejected as `CorruptSignature`.
  - `test_expired_signature`: Validity lifetimes outside `[valid_from, valid_until]` tested and rejected as `ExpiredSignature`.
  - `test_trust_store_serialization`: Full serialization round-trip verified.
