# WinIOS Campaign 233 — Stage 233.6 Deliverable
## Publisher Trust Chain & Role-Based Policy Verification Audit

### 1. Executive Summary & Objective
Stage 233.6 audits the publisher trust chain, role hierarchy, revocation mechanics, and serialization logic in `src/security/publisher_trust_store.h` and `src/security/publisher_trust_store.cpp`.

Target Test Suite: `stage233_6_trust_chain_tests.exe`

---

### 2. Trust Model & Privilege Separation Architecture

The WinIOS platform implements a 3-tier publisher role hierarchy:

```
        +-----------------------------------+
        |      Root Authority (Anchor)      |
        |  com.apple.research.root (anchor) |
        +-----------------+-----------------+
                          |
        +-----------------v-----------------+
        |          Platform Tier            |
        |  Allowed: System services, drivers|
        +-----------------+-----------------+
                          |
        +-----------------v-----------------+
        |         Developer Tier            |
        |  Allowed: Sandboxed guest apps    |
        +-----------------------------------+
```

#### Policy Enforcement Matrix

| Publisher Role | Development Policy | StrictProduction Policy | System Entitlements Allowed? |
| :--- | :--- | :--- | :--- |
| **RootAuthority** | Trusted (`PlatformSigned`) | Trusted (`PlatformSigned`) | Yes (Platform Root) |
| **Platform** | Trusted (`PlatformSigned`) | Trusted (`PlatformSigned`) | Yes |
| **Developer** | Trusted (`DeveloperSigned`) | **REJECTED** (`is_trusted = false`) | **NO** (Restricted sandbox only) |
| **Revoked Key/Pub** | **REJECTED** (`RevokedPublisher`) | **REJECTED** (`RevokedPublisher`) | No |

---

### 3. Remediation of Deserialization Bug

#### Defect Identified (SEC-233-01)
During deserialization of the trust store text format in `PublisherTrustStore::deserialize`, lines matching `revoked_publisher=<id>` were placed in an `else` branch guarded by `if (in_key)`. Because `in_key` was asserted upon parsing the first `[key]` stanza and never reset to `false`, all trailing `revoked_publisher` records were silently bypassed, failing to restore publisher-wide revocations on store reload.

#### Fix Applied
Restructured `PublisherTrustStore::deserialize` so that whenever `k == "revoked_publisher"` is encountered:
1. Any active pending `current_key` is registered into the store.
2. `in_key` is reset to `false`.
3. `revoke_publisher(v)` is called immediately, ensuring complete persistence of publisher blacklists across restarts.

---

### 4. Test Verification Results

| Test # | Test Name | Scenarios Verified | Result |
| :--- | :--- | :--- | :--- |
| **Test 1** | Role Hierarchy & Policy Enforcement | Root, Platform, and Developer signature generation and verification under `Development` and `StrictProduction` policies. Enforces strict production rejection of Developer keys. | **PASSED** |
| **Test 2** | Key and Publisher Revocation | Granular key revocation (`is_key_revoked`) and cascading publisher-wide revocation (`revoke_publisher`). Revoked packages immediately fail verification. | **PASSED** |
| **Test 3** | Key Rotation | Seamless rotation: old key is automatically retired and revoked while the replacement key is admitted and verifies new payloads. | **PASSED** |
| **Test 4** | Temporal Validity & Expiration | Rejection of pre-dated signatures (`current_time < valid_from`) and expired signatures (`current_time > valid_until`). Correct acceptance within valid time windows. | **PASSED** |
| **Test 5** | Unknown Key & Publisher Mismatch | Rejection of unregistered signing keys (`UnknownKey`) and rejection of signature envelopes claiming unauthorized publisher IDs (`CorruptSignature`). | **PASSED** |
| **Test 6** | Trust Store Serialization Roundtrip | Full export (`serialize()`) and import (`deserialize()`) of multi-key store with Root, Platform, Developer, revoked keys, and revoked publishers. | **PASSED** |

Both `RelWithDebInfo` and `Debug` MSVC C++20 builds passed with 0 errors and 0 warnings.
