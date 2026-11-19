# Campaign 231 — Stage 231.3: Package Signature and Publisher Identity Architecture

## 1. Executive Summary & Purpose
This specification defines the cryptographic authentication architecture for WinIOS application packages (`.wios` containers). In previous campaigns, package validation relied exclusively on per-file SHA-256 hashes and arbitrary string comparisons, which could not distinguish between data integrity and publisher authenticity.

Campaign 231 introduces the **Authenticated Package Envelope (`WIOSPKG3-AUTH`)**, establishing:
- Clear separation between **data integrity** (SHA-256 hash digests) and **publisher authenticity** (cryptographic signatures verified against a trusted publisher root).
- Verifiable publisher identities with role-based privilege tiers (`Platform` vs `Developer`).
- Comprehensive authenticated byte ranges covering the container header, manifest, directory table, and binary payloads.
- Strict trust policy states: `Unsigned`, `DeveloperSigned`, `PlatformSigned`, `RevokedPublisher`, `CorruptSignature`, and `ExpiredSignature`.
- Dual operating modes: Development Mode (permissive of registered developer keys) and Production/Strict Mode (requiring genuine Platform signatures).

---

## 2. Authenticated Package Envelope Layout

An authenticated package adheres to the `WIOSPKG3` container specification with header flag `0x01` (`PKG_FLAG_SIGNED`), terminating with an authenticated trailer block (`WIOSAUTH`):

```
+---------------------------------------------------------------+
| Header: Magic ("WIOSPKG3"), Version (3), Flags, Total Size    | (48 bytes)
+---------------------------------------------------------------+
| Manifest Data: Serialized PackageManifest text                | (Variable)
+---------------------------------------------------------------+
| Directory Table: File paths, types, flags, offsets, digests  | (Variable)
+---------------------------------------------------------------+
| File Payloads: Executable binary, assets, data blocks         | (Aligned 16B)
+===============================================================+ <-- End of Authenticated Payload
| Trailer Magic: "WIOSAUTH"                                     | (8 bytes)
+---------------------------------------------------------------+
| Trailer Header: Version (1), Algorithm ID (uint32)            | (8 bytes)
+---------------------------------------------------------------+
| Publisher ID Length (uint16) + Publisher ID String            | (2 + N bytes)
+---------------------------------------------------------------+
| Key ID Length (uint16) + Key ID String                        | (2 + M bytes)
+---------------------------------------------------------------+
| Timestamp Not Before (uint64) + Not After (uint64)            | (16 bytes)
+---------------------------------------------------------------+
| Signature Length (uint16) + Signature Bytes (32B / 64B)        | (2 + S bytes)
+---------------------------------------------------------------+
```

### 2.1 Authenticated Byte Ranges
The cryptographic signature covers all bytes preceding the `WIOSAUTH` trailer magic:
$$\text{AuthenticatedBytes} = \text{PackageBytes}[0 \dots \text{Offset}(\text{WIOSAUTH}) - 1]$$

The authentication input passed to the MAC/signature engine is formed by domain-separated binding:
$$\text{AuthData} = \text{"WIOS-PKG-AUTH-V1:"} \parallel \text{AuthenticatedBytes} \parallel \text{PublisherID} \parallel \text{KeyID} \parallel \text{NotBefore} \parallel \text{NotAfter}$$

Any modification to any byte of the header, manifest, directory structure, individual file payload, or publisher metadata causes immediate verification failure.

---

## 3. Cryptographic Signature Algorithms

The architecture supports two signature schemes for the Windows-hosted ARM64 research environment:

1. **HMAC-SHA256 Authenticated Envelope (`ALG_HMAC_SHA256 = 0x0001`):**
   - Utilizes a 256-bit symmetric publisher signing key securely registered in the `PublisherTrustStore`.
   - Uses RFC 2104 standard HMAC with SHA-256:
     $$\text{HMAC}(K, m) = H((K \oplus \text{opad}) \parallel H((K \oplus \text{ipad}) \parallel m))$$
   - Verified via constant-time comparison (`constant_time_equals`) against the expected tag.
   - Provides full authenticity and tamper resistance with zero external dependencies.

2. **Ed25519 Digital Signatures (`ALG_ED25519 = 0x0002`):**
   - Asymmetric Ed25519 public key verification for production-grade provenance.
   - Supported where platform cryptography provider is configured.

---

## 4. Publisher Identity Model

Each signing entity is represented in the `PublisherTrustStore` by a `PublisherIdentity` record:

```cpp
enum class PublisherRole : std::uint32_t {
    Developer = 1,  // Standard sandboxed applications
    Platform = 2    // Core platform services, unrestricted system capabilities
};

struct PublisherIdentity {
    std::string publisher_id;      // e.g. "com.winios.platform.root"
    std::string organization_name; // e.g. "WinIOS Platform Authority"
    std::string key_id;            // e.g. "KEY-PLATFORM-2026-A16"
    PublisherRole role;            // Developer or Platform
    std::vector<std::uint8_t> key_data; // 32-byte secret / public key
    std::uint64_t valid_from{0};   // Epoch timestamp
    std::uint64_t valid_until{0};  // Expiration (0 = lifetime)
    bool is_revoked{false};        // Explicit revocation
};
```

---

## 5. Signature Verification States & Trust Policy

### 5.1 Verification States
When evaluating an application package, `PackageTrustStore::verify_package` yields one of six discrete states:

| State | Definition | Security Result |
| :--- | :--- | :--- |
| `Unsigned` | Package lacks signature block or signed flag. | Allowed in permissive dev mode only; denied in strict. |
| `DeveloperSigned` | Valid signature matching an active `Developer` key in trust store. | Allowed in dev mode; denied in strict production mode. |
| `PlatformSigned` | Valid signature matching an active `Platform` key in trust store. | Allowed in all modes; grants platform capabilities. |
| `RevokedPublisher` | Key ID or Publisher ID matches a revoked entry. | Strictly rejected in all modes (`UntrustedSignature`). |
| `CorruptSignature` | Signature does not match calculated MAC, or trailer is malformed. | Strictly rejected in all modes (`ChecksumMismatch`). |
| `ExpiredSignature` | Current system time is outside `[valid_from, valid_until]`. | Strictly rejected in all modes (`UntrustedSignature`). |

### 5.2 Trust Policy Enforcement Matrix

| Policy Mode | Unsigned | DeveloperSigned | PlatformSigned | Revoked | Corrupted |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Development Mode** | Optional Warning | Accepted | Accepted | **REJECTED** | **REJECTED** |
| **Strict Production** | **REJECTED** | **REJECTED** | Accepted | **REJECTED** | **REJECTED** |

---

## 6. Security Guarantees & Anti-Tampering Properties

1. **Non-Repudiation & Provenance:** An application's publisher is verified against the trusted store; applications cannot spoof platform or publisher identities.
2. **Anti-Substitution:** Every file entry and its hash is tied into the authenticated payload; substituting an executable binary invalidates the outer signature.
3. **Anti-Replay / Revocation:** Revoking a compromised key in `PublisherTrustStore` immediately invalidates all packages signed by that key upon the next installation or verification.
4. **Timing Attack Resilience:** All digest and signature comparisons execute in constant time via `constant_time_equals`.
