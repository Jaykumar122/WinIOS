# WinIOS Asymmetric Signing and Keystore Developer Guide
## Campaign 232 — Developer Guide

This guide details the asymmetric public-key signature system and secure key management architecture introduced in Campaign 232 for the Windows 11-hosted WinIOS ARM64 research platform.

---

## 1. Overview

WinIOS uses **Ed25519 (RFC 8032)** for all package signing and verification, replacing symmetric HMAC secrets. This ensures:
- Private signing keys remain securely in developer custody and are never shared with or stored in the runtime environment.
- Public verification keys are registered in the runtime `PublisherTrustStore` within specific authority tiers.
- A compromised runtime verification key cannot forge signed application packages.

---

## 2. Developer Key Management

The developer keystore is managed via `src/security/developer_keystore.h` and the CLI interface `winios-cli`.

### Key Generation
To generate a new Ed25519 developer signing identity:
```powershell
winios-cli keygen --name "MyCompany" --tier developer --out mykey.jwk
```
By default, private keys are encrypted using **Windows Data Protection API (DPAPI)**, binding decryption to the current Windows user account.

### Key Export & Public Key Extraction
To extract the public verification key (32 bytes Ed25519) for registration into the platform trust store:
```powershell
winios-cli key-export-pub --in mykey.jwk --out mypub.json
```

---

## 3. Package Signing

Application packages in the canonical `WIOSPKG3` format are signed using:
```powershell
winios-cli app-sign --package MyApp.wiospkg --key mykey.jwk --out MyApp_signed.wiospkg
```

This appends the `WIOSAUTH` v2 trailer containing:
- Magic: `"WIOSAUTH"`
- Trailer Version: `0x0002`
- Algorithm ID: `0x0002` (Ed25519)
- Key ID: 32-byte SHA-256 digest of the public key
- Timestamp: 64-bit UTC timestamp
- Signature: 64-byte Ed25519 signature $(R, S)$

---

## 4. Trust Store Administration

The platform trust store enforces multi-tier authority:
1. **Root Authority**: Master authority for signing intermediate and system updates.
2. **Platform Authority**: Privileged platform daemons and system services.
3. **Developer Identity**: Sandboxed user applications.

To register a developer public key in the trust store:
```powershell
winios-cli trust-add-key --cert mypub.json --store trust_store.json
```

To revoke a compromised key:
```powershell
winios-cli trust-revoke --key-id <64-hex-chars> --store trust_store.json
```

---

## 5. Security & Verification Rules

1. **Algorithm Downgrade Protection**: The runtime rejects packages whose signature algorithm does not strictly match the registered public key type.
2. **Validity Windows**: Signatures must fall within the key's `[not_before, not_after]` validity window.
3. **Canonical Format**: The package body must strictly follow the canonical sorting rules of `WIOSPKG3`.
