# Stage 234.6 — DPAPI and Keystore Threat Model Report

## Executive Summary

Stage 234.6 established a formal, evidence-based threat model for developer key storage in WinIOS (`src/security/developer_keystore.cpp`), analyzing the cryptographic protections provided by Windows DPAPI, PBKDF2-HMAC-SHA256, and runtime trust store key separation.

Test executable `stage234_6_keystore_threat_tests` was created, registered, built, and executed across both `RelWithDebInfo` and `Debug` configurations, confirming negative input handling, corrupted DPAPI detection, public-key substitution detection, and strict key isolation.

---

## 1. DPAPI Scope & Threat Model Analysis

In `DeveloperKeyStore::dpapi_protect`, the implementation calls:
```cpp
CryptProtectData(&data_in, L"WinIOS Developer Key", nullptr, nullptr, nullptr,
                 CRYPTPROTECT_UI_FORBIDDEN, &data_out);
```

### Analysis of Security Boundaries:
1. **User-Scope (Default):**
   - Because `CRYPTPROTECT_LOCAL_MACHINE` is **omitted**, Windows DPAPI defaults to **User-Scope**.
   - The encryption master key is tied to the logged-in Windows user's logon credentials (SID + password hash).
   - **Protection Achieved:** Other user accounts on the same Windows host and offline attackers extracting the raw hard drive or `.json` keystore file cannot decrypt the private seed.
2. **Explicit Threat Limitation (Active Session Vulnerability):**
   - **Critical Disclosure:** DPAPI does **not** protect secrets against malicious code, malware, or compromised scripts executing inside the **same user's active desktop session**. Any process running under the developer's Windows credentials can call `CryptUnprotectData` and decrypt the blob without prompting.
3. **Defense-in-Depth via Combined Passphrase (`DPAPIAndPassphrase`):**
   - To mitigate active-session key compromise, `DeveloperKeyStore` offers `KeystoreProtection::DPAPIAndPassphrase`.
   - The private seed is first encrypted using PBKDF2-HMAC-SHA256 (600,000 iterations, 32-byte salt) with a developer-chosen passphrase.
   - The resulting encrypted payload is then sealed with DPAPI.
   - Even if malware in the user session calls DPAPI, the inner payload remains protected by the passphrase.

---

## 2. PBKDF2 Parameterization & Key Derivation

When passphrase protection is active:
- **Salt:** 32 bytes generated via Windows CNG CSPRNG (`SecureRandom::get_random_bytes`).
- **Algorithm:** PBKDF2-HMAC-SHA256.
- **Iteration Count:** 600,000 rounds (exceeding OWASP 2023 recommendations for PBKDF2-HMAC-SHA256).
- **Key Separation:** Derived 64 bytes are split into two independent 32-byte keys:
  - $K_{\text{enc}} = \text{Derived}[0..31]$ (Used for ciphertext encryption)
  - $K_{\text{mac}} = \text{Derived}[32..63]$ (Used for HMAC-SHA256 authenticated integrity tag)
- **Integrity Tag:** Verified in constant time via `constant_time_equals` prior to decryption.

---

## 3. Strict Publisher Trust Store Key Separation Invariant

A central principle of asymmetric packaging in WinIOS is that **private signing keys must never enter the runtime publisher trust store**.

| Component | Storage Role | Permitted Data | Prohibited Data |
| :--- | :--- | :--- | :--- |
| **`DeveloperKeyStore`** | Offline / Developer workstation | Public key (32 bytes), encrypted private seed (32 bytes), metadata | Unprotected private keys |
| **`PublisherTrustStore`** | Runtime / Guest emulator kernel | Public key (32 bytes), key ID, validity dates, CRL | Private key seeds, HMAC shared secrets |

### Empirical Verification:
In `stage234_6_keystore_threat_tests`, `DeveloperKeyStore::export_public_key()` was verified:
- Output `PublisherKey` contains strictly 32 bytes of public key.
- Constant-time comparison confirms `key_bytes != private_key_seed`.
- When registered into `PublisherTrustStore`, zero private key material is resident in the trust store memory.

---

## 4. Negative and Tampering Defenses Verified

1. **Non-Existent File:** Returns `std::nullopt` cleanly.
2. **Corrupted JSON Syntax:** Returns `std::nullopt` without throwing or crashing.
3. **Public Key Substitution Attack:** An attacker modifying the plaintext `"public_key"` in the JSON file while keeping the encrypted private key is caught: on load, `DeveloperKeyStore` derives the public key from the decrypted seed and compares it with the JSON field; mismatch causes immediate rejection.
4. **Corrupted DPAPI Ciphertext:** Bit flips in base64 ciphertext return `std::nullopt` via `CryptUnprotectData` error detection.
5. **Wrong Passphrase:** HMAC tag verification failure prevents decryption and wipes intermediate buffers.
