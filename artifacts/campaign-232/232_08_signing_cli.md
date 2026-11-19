# Stage 232.8 Deliverable — Package Signing & Verification CLI

## 1. Overview
Stage 232.8 extends the WinIOS developer command-line interface (`src/tools/winios_app_cli.h`) with a comprehensive suite of key management, package signing, and trust store administration commands.

## 2. Implemented Commands
1. **`keygen`:**
   - Generates an Ed25519 developer signing keypair and writes a protected keystore file (`.devkey`).
   - Options: `--publisher-id <id>`, `--org <name>`, `--out <file>`, `--key-id <id>`, `--passphrase <pass>`, `--protection dpapi|passphrase|none`, `--json`.
2. **`key-export-pub`:**
   - Exports the public verification key component from a keystore into a `.pub` file consumable by trust stores.
   - Options: `--key <file>`, `--out <file>`, `--passphrase <pass>`, `--json`.
3. **`app-sign`:**
   - Cryptographically signs an application package (`.wiospkg`) with the developer key using the `WIOSAUTH v2` trailer format.
   - Options: `--pkg <file>`, `--key <file>`, `--out <file>`, `--passphrase <pass>`, `--valid-days <n>`, `--flags <f>`, `--json`.
4. **`app-verify`:**
   - Evaluates package authenticity, integrity, and trust against a given trust store and policy (`dev` vs `prod`).
   - Options: `--pkg <file>`, `--trust-store <file>`, `--policy dev|prod`, `--json`.
   - Returns exit code 0 on trust success; exit code 2 on verification failure.
5. **`trust-add-key`:**
   - Imports public keys into an existing or new trust store configuration file.
   - Options: `--trust-store <file>`, `--key-file <file>`, `--root`, `--json`.
6. **`trust-revoke`:**
   - Revokes a specific key ID or an entire publisher domain inside a trust store.
   - Options: `--trust-store <file>`, `--key-id <id>` or `--publisher-id <id>`, `--json`.

## 3. Test Verification
- Executed unit tests in `tests/unit/stage232_8_signing_cli_tests.cpp` (registered as CTest #929).
- Tested end-to-end developer CLI workflow:
  1. `keygen` generates identity and stores `.devkey`.
  2. `key-export-pub` exports `.pub`.
  3. `trust-add-key` imports `.pub` into a fresh `trust_store.ini`.
  4. `app-sign` signs raw package into signed WIOSAUTH v2 envelope.
  5. `app-verify` validates package under Development policy (pass) and StrictProduction policy (fail on dev key).
  6. `trust-revoke` revokes publisher ID; subsequent verify immediately fails.
  7. CLI invalid argument handling and exit codes validated.
- Build Status: Zero warnings and zero errors under MSVC `/W4 /WX /permissive-` in both Debug and RelWithDebInfo.
