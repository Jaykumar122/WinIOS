# WinIOS Package Authentication Specification v2 (`WIOSAUTH`)
## Campaign 232 — Specification Document

---

## 1. Specification Overview

The `WIOSAUTH` trailer format provides cryptographic authenticity, integrity, non-repudiation, and publisher identity binding for WinIOS `WIOSPKG3` application packages.

---

## 2. Binary Layout

The trailer is appended directly to the end of a canonical `WIOSPKG3` package container:

```text
Offset (Bytes)  Field Size  Field Name        Description
0               8           magic             ASCII "WIOSAUTH" (0x57, 0x49, 0x4F, 0x53, 0x41, 0x55, 0x54, 0x48)
8               2           version           Trailer specification version (0x0002 for v2)
10              2           algorithm_id      Signature algorithm:
                                                0x0001 = HMAC-SHA256 (Legacy / Testing)
                                                0x0002 = Ed25519 (RFC 8032)
12              32          key_id            32-byte SHA-256 digest of public verification key
44              8           timestamp         64-bit UTC UNIX timestamp (seconds since epoch)
52              16          reserved          16 bytes zero padding for future extensions
68              64          signature         64-byte Ed25519 signature (R, S) or HMAC digest + padding
----------------------------------------------------------------------------------------------------
Total Length: 132 bytes
```

---

## 3. Cryptographic Signature Domain

To ensure complete cryptographic binding, the signature is computed over a digest encompassing the package body and all header metadata:

$$\text{DataToSign} = \text{SHA-256}(\text{Canonical Package Body}) \parallel \text{AlgorithmID (2B)} \parallel \text{KeyID (32B)} \parallel \text{Timestamp (8B)}$$

This construction prevents:
- **Trailer Splicing**: A signature cannot be extracted and appended to a different package body.
- **Key Substitution**: The signature cannot be claimed by a different key identifier.
- **Timestamp Tampering**: The timestamp cannot be altered without failing verification.
- **Algorithm Confusion**: An HMAC-SHA256 signature cannot be verified under an Ed25519 public key.

---

## 4. Verification Algorithm

When validating a package `P`:
1. Check that `size(P) >= 132`.
2. Inspect the last 132 bytes for the `"WIOSAUTH"` magic.
3. Parse `version`, `algorithm_id`, `key_id`, `timestamp`, and `signature`.
4. Query `PublisherTrustStore` for `key_id`. If not found, return `UntrustedPublisher`.
5. Check if `key_id` is present on the revocation list. If revoked, return `KeyRevoked`.
6. Verify that `algorithm_id` matches the registered certificate algorithm. If mismatch, return `AlgorithmMismatch`.
7. Verify that `timestamp` is within the certificate validity window `[not_before, not_after]`.
8. Reconstruct `DataToSign` over the body bytes $0 \dots (\text{size}(P) - 132)$.
9. Verify the 64-byte signature using the certificate's Ed25519 public key.
