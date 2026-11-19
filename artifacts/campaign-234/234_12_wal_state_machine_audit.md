# Stage 234.12: WAL State Machine Source-of-Truth Audit

## Executive Summary
Stage 234.12 reconciles and audits the Write-Ahead Log (WAL) transaction state machine in `PackageInstaller` (`src/packaging/package_installer.h` and `src/packaging/package_installer.cpp`). This resolves historical documentation ambiguities (such as referring to "9 states" vs "11 states") and rigorously distinguishes between **integrity checksums** and **cryptographic authentication**.

---

## 1. Complete WAL State Machine Enumeration (11 Enum States)

`enum class PackageInstaller::InstallerState : std::uint32_t`:

| State Value | Enum Identifier | String Name | Category | Description |
| :---: | :--- | :--- | :--- | :--- |
| **0** | `Idle` | `"Idle"` | Resting | Initial state prior to transaction initiation. |
| **1** | `Initiated` | `"Initiated"` | Forward (1) | Transaction ID allocated; staging transaction directory created. |
| **2** | `Validating` | `"Validating"` | Forward (2) | Manifest validity and bundle ID conflict checking underway. |
| **3** | `ManifestParsed` | `"ManifestParsed"` | Forward (3) | Manifest successfully validated; memory structures initialized. |
| **4** | `SecurityVerified` | `"SecurityVerified"` | Forward (4) | Bundle file paths verified against traversal rules and normalized. |
| **5** | `Staged` | `"Staged"` | Forward (5) | Staging bundle directory created on VFS. |
| **6** | `Extracting` | `"Extracting"` | Forward (6) | File payloads extracted to staging directory. |
| **7** | `Extracted` | `"Extracted"` | Forward (7) | Pre-commit SHA-256 digest re-verification of all staged files. |
| **8** | `Registering` | `"Registering"` | Forward (8) | Atomic move (`rename`) to target path; application registry updated. |
| **9** | `Committed` | `"Committed"` | Forward (9) / Terminal | Final forward state; journal committed; staging cleanup. |
| **10** | `Aborted` | `"Aborted"` | Terminal Failure | Written on error/rollback; triggers staging rollback and cleanup. |

### Compatibility Aliases:
- `Pending` = `Initiated` (1)
- `Validated` = `SecurityVerified` (4)
- `Committing` = `Registering` (8)
- `RolledBack` = `Aborted` (10)

### State Count Reconciliation:
- **Total distinct enum values:** 11 (`Idle` [0] through `Aborted` [10]).
- **Forward active installation phases:** 9 forward states (`Initiated` [1] through `Committed` [9]).
- **Crash failure points tested before commit:** 8 pre-commit states (states 1 through 8).

---

## 2. Integrity Checksum vs Authentication Property

### Journal Checksum Algorithm:
Every transition writes a journal record to `/staging/<tx_id>/journal.json` containing:
$$\text{checksum} = \text{SHA-256}(\text{tx\_id} \parallel "|" \parallel \text{bundle\_id} \parallel "|" \parallel \text{state} \parallel "|" \parallel \text{is\_update} \parallel "|" \parallel \text{target\_dir} \parallel "|" \parallel \text{backup\_dir})$$

### Source-of-Truth Properties:
1. **Integrity Checksum (Empirical Detection of Bitflips & Truncation):**
   - Mutating any field or bit in the journal record produces an immediate checksum mismatch.
   - During recovery (`reconcile_and_recover()`), journals with mismatched checksums are recognized as corrupted and aborted.
2. **Not an Authenticated Signature (No Keyed Secret):**
   - The checksum uses unkeyed SHA-256. It does not use HMAC, private keys, or asymmetric cryptography.
   - Any local process with write permission to the journal file could recompute the SHA-256 hash.
   - Therefore, the WAL journal is officially defined as an **integrity-checksummed log**, NOT an **authenticated log**.

---

## 3. Test Verification
The dedicated suite `tests/unit/stage234_12_wal_state_machine_audit_tests.cpp` was compiled with MSVC C++20 (`/W4 /WX /permissive-`) and executed:
- `RelWithDebInfo`: Passed 100%
- `Debug`: Passed 100%
