# WinIOS Campaign 233 — Stage 233.8 Deliverable
## Key Rotation Lifecycle & Revocation Persistence Security Audit

### 1. Executive Summary & Objective
Stage 233.8 audits the end-to-end key rotation and revocation lifecycle within `PublisherTrustStore` (`src/security/publisher_trust_store.h`, `src/security/publisher_trust_store.cpp`).
The goal is to verify that keys can be rotated across generations (Gen 1 -> Gen 2 -> Gen 3) with immediate revocation of retired keys, that revocations persist faithfully across trust store serialization cycles, and that publishers cannot circumvent revocations by registering fresh keys.

Target Test Suite: `stage233_8_key_rotation_revocation_tests.exe`

---

### 2. Multi-Generation Key Rotation Model

When a publisher rotates keys via `PublisherTrustStore::rotate_key(old_key_id, new_key)`:
1. The `old_key_id` is immediately added to the revoked key registry (`revoked_keys_`) and marked `is_revoked = true`.
2. Any packages signed with the retired key immediately fail verification with `SignatureStatus::RevokedPublisher`.
3. The replacement key (`new_key`) is registered into `keys_` and begins signing and verifying packages.

```
[ Gen 1: key-v1 ] --(rotate_key)--> [ Gen 1: REVOKED ]
                                    [ Gen 2: key-v2 (ACTIVE) ]
                                          |
                                     (rotate_key)
                                          |
                                          v
                                    [ Gen 1: REVOKED ]
                                    [ Gen 2: REVOKED ]
                                    [ Gen 3: key-v3 (ACTIVE) ]
```

---

### 3. Re-Registration Immunity & Cascading Revocation

To prevent malicious publishers from evading revocation by generating and registering fresh public keys under an existing publisher ID:
- `PublisherTrustStore::register_key` checks `revoked_publishers_.contains(key.publisher_id)`.
- If the publisher ID is blacklisted, the key is unconditionally marked `is_revoked = true` upon registration.
- Any attempt to verify a package signed with this sneaky key immediately fails with `SignatureStatus::RevokedPublisher`.

---

### 4. Test Verification Results

| Test # | Test Name | Scenarios Verified | Result |
| :--- | :--- | :--- | :--- |
| **Test 1** | Multi-Generation Key Rotation | Gen 1 -> Gen 2 -> Gen 3 rotation. Verified that newly rotated keys succeed while retired keys from previous generations are rejected as `RevokedPublisher`. | **PASSED** |
| **Test 2** | Revocation Persistence Across Reload | Trust store with active, explicitly revoked, and publisher-revoked keys serialized to text, loaded into a brand-new instance, and re-verified. All revocation statuses persisted exactly. | **PASSED** |
| **Test 3** | Post-Revocation Key Registration Defense | Attempted to register a new key under a blacklisted publisher. Verified automatic admission into revoked status and rejection of packages signed with it. | **PASSED** |

Both `RelWithDebInfo` and `Debug` MSVC C++20 builds passed with 0 errors and 0 warnings.
