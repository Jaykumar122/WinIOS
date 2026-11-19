# Stage 232.5 Deliverable — Key Generation & Secure Storage

## 1. Overview
Stage 232.5 implements developer identity key generation, secure credential storage, and public verification key export (`src/security/developer_keystore.h`, `src/security/developer_keystore.cpp`).

This component strictly enforces the security boundary between developer-held private signing keys and runtime-held public verification keys:
- Private keys (32-byte Ed25519 seeds) are protected at rest via Windows Data Protection API (DPAPI) and/or PBKDF2-HMAC-SHA256 authenticated encryption.
- Public keys (32-byte Ed25519 public keys) are safely exported into standard trust store configuration files (`.pub`).
- The system guarantees that private keys are never included in exported public key artifacts, packages, or runtime trust stores.

## 2. Key Architecture & Features
1. **Developer Identity Representation (`DeveloperIdentity`):**
   - Fields: `publisher_id`, `organization_name`, `key_id`, `role`, `algorithm` (Ed25519), `created_at`, `valid_from`, `valid_until`.
   - `wipe_private_key()` ensures zeroization of sensitive private seed buffers upon disposal.
2. **Key Storage Protection Modes (`KeystoreProtection`):**
   - `DPAPI`: Hardware/OS-bound encryption tied to the Windows user account via `CryptProtectData` (`CRYPTPROTECT_UI_FORBIDDEN`).
   - `Passphrase`: Key derivation using PBKDF2 with HMAC-SHA256 (5,000 iterations, 16-byte CSPRNG salt), followed by CTR stream encryption and HMAC-SHA256 message authentication.
   - `DPAPIAndPassphrase`: Dual-layer protection combining DPAPI and user passphrase.
3. **Tamper-Evident Keystore Format (`WIOSDEVKEY1`):**
   - Incorporates full-file SHA-256 integrity verification (`checksum_hex`).
   - Any tampering or modification of header metadata or ciphertext causes immediate rejection on load.
4. **Public Key Export (`PublisherTrustStore` Compatible):**
   - `export_public_key()` and `export_public_key_text()` only copy the 32-byte public key.
   - Outputs INI format directly consumable by `PublisherTrustStore::deserialize()`.

## 3. Test Verification
- Executed unit tests in `tests/unit/stage232_5_key_storage_tests.cpp` (registered as CTest #926).
- Tests passed:
  1. `test_generate_identity`: Verified Ed25519 keypair generation and field initialization.
  2. `test_wipe_private_key`: Verified secure zeroization of private key memory.
  3. `test_export_public_key_and_safety`: Verified that exported keys match public keys, do not leak private seeds, and deserialize cleanly into `PublisherTrustStore`.
  4. `test_dpapi_protect_unprotect`: Verified Windows DPAPI roundtrip encryption and decryption.
  5. `test_passphrase_encryption_roundtrip`: Verified PBKDF2-HMAC-SHA256 authenticated stream cipher roundtrip and rejection of incorrect passwords or corrupted tags.
  6. `test_keystore_file_storage`: Verified file saving/loading under DPAPI, Passphrase, tamper-detection rejection, and public key file generation.
- Build Status: Zero warnings and zero errors under MSVC `/W4 /WX /permissive-` in both Debug and RelWithDebInfo.
