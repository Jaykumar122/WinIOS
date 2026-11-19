# Stage 231.11 — Document Grant and Association Security

## Overview
Stage 231.11 addresses finding SEC-231-05 by binding cross-process document grants to `ProcessHandle` (`pid` + `generation`), replacing sequential grant tokens with 128-bit CSPRNG hex tokens, hardening canonical path validation, and preventing unauthorized document access across recycled process slots.

## Key Implementation Details
1. **Strict Canonical Path Validation**:
   - `DocumentRegistry::sanitize_and_validate_path` now integrates `winios::packaging::CanonicalPathValidator::validate_and_normalize_bundle_path`.
   - Rejects path traversal (`..`), percent-encoded traversals (`%2e`, `%2f`, `%5c`), backslashes, Windows reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`), Alternate Data Streams (`:`), trailing spaces/dots, control characters, depth > 16, and length > 256.
   - Enforces permitted virtual document roots (`/Documents`, `/Shared`, `/Downloads`, `/tmp`, `/Users`, `/var/mobile`).
2. **128-Bit CSPRNG Grant Tokens**:
   - `DocumentAccessGrant` now generates `grant_token_hex`, a 32-character hexadecimal string with 128 bits of cryptographic entropy via `winios::security::SecureRandom::get_random_hex(16)`.
   - Dual-index lookup via token ID and `grants_by_hex_` map.
   - Revocation supported by token ID and token hex string (`revoke_grant_by_hex`).
3. **Generational Grant Binding & Recycled PID Protection**:
   - `DocumentAccessGrant` records `grantor_generation` and `grantee_generation`.
   - `create_grant` and `check_access` accept `ProcessHandle` parameters and consult `ProcessIdentityManager`.
   - When a process terminates and its slot is recycled by a new process instance with an incremented generation, all existing grants bound to the prior occupant are rejected.
   - `list_documents_for_process` filters out any documents where the grant's recorded generation does not match the querying PID's active generation.
4. **Process Handle Cleanup**:
   - `revoke_all_for_process_handle(ProcessHandle)` cleans up grants specifically associated with the terminating process instance without disturbing grants belonging to unrelated processes.
5. **Document Association Manager Integration**:
   - `DocumentAssociationManager::handoff_document` populates `target_generation` and `grant_token_hex` in `DocumentHandoffResult`.

## Verification
- **Test Suite**: `tests/unit/stage231_11_doc_grant_security_tests.cpp` (CTest #913).
- **Test Results**:
  - RelWithDebInfo: Test #913 Passed in 0.04s.
  - Debug: Test #913 Passed in 0.06s.
  - Regressions: Baseline tests #861, #863, #864, #866, #902 passed 100% in 0.38s.
