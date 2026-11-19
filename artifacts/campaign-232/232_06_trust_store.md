# Stage 232.6 Deliverable — Trust Store & Certificate Policy

## 1. Overview
Stage 232.6 upgrades `PublisherTrustStore` to support asymmetric public verification keys, root trust anchors, key rotation, publisher and key-level revocations, validity windows, algorithm downgrade rejection, and policy enforcement across `Development` and `StrictProduction` modes (`src/security/publisher_trust_store.h`, `src/security/publisher_trust_store.cpp`).

## 2. Key Architecture & Features
1. **Tiered Roles & Root Trust Anchors:**
   - Supported roles: `Developer`, `Platform`, `RootAuthority`.
   - `register_root_anchor()` registers root certificates; `is_root_anchor()` and `get_root_anchor()` identify the platform trust anchor.
2. **Key Rotation & Multi-Key Management:**
   - Multiple active keys per publisher are supported (`get_keys_for_publisher()`).
   - `rotate_key(old_key_id, new_key)` automatically transitions an old key to revoked status while registering the new key.
3. **Strict Revocation:**
   - `revoke_key(key_id)` revokes individual compromise keys.
   - `revoke_publisher(publisher_id)` revokes an entire publisher domain and all its associated keys.
4. **Strict Algorithm Downgrade Rejection (Addressing SEC-232-08):**
   - The verification engine strictly cross-checks the declared signature algorithm in the package trailer (`alg`) against the registered algorithm in `PublisherKey::algorithm`.
   - Any attempt to downgrade an Ed25519-registered key using an HMAC signature (or vice-versa) is immediately rejected with `SignatureStatus::AlgorithmMismatch`.
5. **Ed25519 Verification & Trust Policies:**
   - `verify_package_signature()` dynamically supports both Ed25519 (64-byte signature, 32-byte public key) and HMAC-SHA256.
   - `TrustPolicy::Development`: Trusts both `PlatformSigned` and `DeveloperSigned` packages.
   - `TrustPolicy::StrictProduction`: Requires `PlatformSigned` (or `RootAuthority`), explicitly rejecting `DeveloperSigned` and unsigned packages as untrusted.

## 3. Test Verification
- Executed unit tests in `tests/unit/stage232_6_trust_store_tests.cpp` (registered as CTest #927).
- Tests passed:
  1. `test_root_anchor`: Root anchor registration, lookup, role verification.
  2. `test_multi_key_and_rotation`: Multi-key inventory and key rotation with previous key revocation.
  3. `test_revocation`: Individual key revocation and publisher-wide revocation.
  4. `test_ed25519_sign_and_verify_policies`: Ed25519 package signing, verification under Development and StrictProduction policies, timestamp window validation.
  5. `test_algorithm_downgrade_rejection`: Mutated algorithm trailer rejected with `SignatureStatus::AlgorithmMismatch`.
  6. `test_tampered_payload_rejected`: Payload bit corruption detected and rejected with `SignatureStatus::CorruptSignature`.
- Build Status: Zero warnings and zero errors under MSVC `/W4 /WX /permissive-` in both Debug and RelWithDebInfo.
