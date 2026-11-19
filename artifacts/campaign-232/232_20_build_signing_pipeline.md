# Stage 232.20: Deterministic Build & Signing Pipeline Verification

## 1. Overview & Objectives
Stage 232.20 established and validated the decoupled application packaging and signing pipeline. In this architecture, canonical packaging (`pack_v3`) operates purely on file descriptors, manifests, and file payloads to construct bit-for-bit deterministic unsigned packages (`WIOSPKG3`), completely independent of the signing mechanism. The signing stage takes the canonical binary artifact, computes the SHA-256 digest over the canonical content, and appends a `WIOSAUTH` v2 trailer signed by Ed25519 or HMAC-SHA256.

## 2. Decoupled Pipeline Architecture

```
[Application Files & Manifest]
               │
               ▼
    AppPackage::pack_v3()
  (Canonical sorting & hashing)
               │
               ▼
     [Unsigned WIOSPKG3] ──────────────────────┐ (Reproducible SHA-256 digest)
               │                                │
               ▼                                │
   PublisherTrustStore::sign_package_v2()       │
  (Binds key, timestamp, flags, content hash)   │
               │                                │
               ▼                                │
       [Signed WIOSPKG3]                        │
               │                                │
               ▼ (Strip trailer & restore hdr)  │
     [Recovered Unsigned] ◄─────────────────────┘ (Exact bit-for-bit match)
```

### Determinism Properties
1. **Packaging Determinism**: Any two packages constructed from identical inputs produce identical byte sequences regardless of insertion order, system platform, or filesystem directory order.
2. **Deterministic Signing**: For any fixed private key, timestamp, validity period, and flags, signing an unsigned package yields bit-for-bit identical signed output.
3. **Unsigned Artifact Recovery**: Stripping the signature trailer and restoring header flag bit 0 and total size recovers the exact byte-for-byte unsigned canonical package.
4. **Key Independence**: Signing the same unsigned canonical package with two distinct developer keys binds each signature independently to the exact same underlying SHA-256 content digest.

## 3. Test Coverage & Results

Test target `stage232_20_build_signing_pipeline_tests` (Test #938) verified:
- **Unsigned Canonical Packaging Determinism**: Verified that inserting files in inverted order produces the exact same `pack_v3` byte output.
- **Deterministic Signing Pipeline**: Verified that signing with a fixed timestamp twice produces byte-for-byte identical output.
- **Unsigned Artifact Recovery**: Verified that extracting the trailer and restoring header fields from a signed package yields the exact original unsigned package.
- **Multi-Key Independence**: Signed a single canonical package with Developer Key A and Developer Key B; both verified independently under `Development` policy in `PublisherTrustStore` and matched the same content digest.
- **Pipeline Tamper Detection**:
  - Mutating 1 byte in the payload area -> detected and rejected with `SignatureStatus::CorruptSignature`.
  - Mutating 1 byte in the trailer signature -> rejected with `SignatureStatus::CorruptSignature`.
  - Timestamp beyond valid window -> rejected with `SignatureStatus::ExpiredSignature`.
  - Revoked key -> rejected with `SignatureStatus::RevokedPublisher`.

All tests passed with 100% success.
