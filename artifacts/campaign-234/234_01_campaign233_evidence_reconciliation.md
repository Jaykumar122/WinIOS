# Stage 234.1 — Campaign 233 Evidence Reconciliation

## Executive Summary

Stage 234.1 resolves discrepancies, reconciles metric counts, clarifies test suite scopes, and corrects inaccurate terminology from Campaign 233 documentation.

WinIOS is an experimental Windows-hosted ARM64 guest application research platform modeling an iPhone 15 research target profile (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`, 16 KB page granule, freestanding Mach-O EL0). It does not claim production iOS compatibility, genuine Apple hardware execution, or third-party cryptographic certification.

---

## 1. Resolution of the 9-State vs. 11-State WAL Discrepancy

In Campaign 233 documentation, the Write-Ahead Logging (WAL) installer state machine was variously referred to as a "9-state machine" and an "11-state machine".

Inspection of `src/packaging/package_installer.h` (`enum class InstallerState`) reveals the exact source of truth:
```cpp
enum class InstallerState : std::uint32_t {
    Idle = 0,
    Initiated = 1,
    Validating = 2,
    ManifestParsed = 3,
    SecurityVerified = 4,
    Staged = 5,
    Extracting = 6,
    Extracted = 7,
    Registering = 8,
    Committed = 9,
    Aborted = 10,
    // Compatibility aliases:
    Pending = Initiated,
    Validated = SecurityVerified,
    Committing = Registering,
    RolledBack = Aborted
};
```

### Reconciliation:
1. **Total State Space (11 States):** The formal enum defines 11 distinct states numbered `0` through `10`.
2. **Forward Installation Path (9 States):** A normal successful installation progresses through 9 active forward states: `Initiated` (1) $\to$ `Validating` (2) $\to$ `ManifestParsed` (3) $\to$ `SecurityVerified` (4) $\to$ `Staged` (5) $\to$ `Extracting` (6) $\to$ `Extracted` (7) $\to$ `Registering` (8) $\to$ `Committed` (9).
3. **Quiescent / Error States (2 States):** `Idle` (0) is the pre-transaction resting state, and `Aborted` (10) is the terminal rollback state.
4. **Fault-Injection Test Coverage (8 States):** Stage 233.13 tested mid-flight crash simulation at all 8 intermediate active states (`Initiated` through `Registering`).

**Canonical Clarification:** The WAL state machine has **11 distinct states** in its lifecycle definition, with **9 forward progress states** during installation.

---

## 2. Reconciliation of Fuzzing Iteration Counts

Campaign 233 cited "5,500 fuzz iterations" in some sections and "1,000 parser" or "2,500 filesystem" in others.

Inspection of `tests/unit/stage233_20_security_fuzzing_tests.cpp` establishes the exact breakdown under seed `kFuzzSeed = 0x233C0DE`:

| Subsystem Fuzz Target | Iteration Count | Injected Mutation Types | Result |
| :--- | :---: | :--- | :---: |
| **Package Container Parser** | 1,000 | Bit flips, byte truncation, corrupt header sizes, zero-fills | 0 crashes, 0 hangs |
| **WIOSAUTH v2 Trailer** | 1,000 | Bit flips across magic, digest, flags, key ID, and signature | 0 unauthorized admits |
| **Canonical Path Validator** | 2,500 | `../`, `CON`, `PRN`, `AUX`, `NUL`, `COM1..9`, `::$DATA`, `~1`, trailing dots/spaces | 0 escaped paths |
| **Guest Memory Validator** | 1,000 | 64-bit addresses, sizes, alignments, permission flags | 0 host dereference faults |
| **Total Fuzz Iterations** | **5,500** | **4 Distinct Attack Surfaces** | **100% Handled** |

**Canonical Clarification:** The figures are fully reconciled: 1,000 (package) + 1,000 (trailer) + 2,500 (filesystem) + 1,000 (memory) = **5,500 total iterations**.

---

## 3. Repository CTest Inventory vs. Campaign Test Scope

The full CMake build definition registers **955 CTest targets** accumulated over the platform's history.

- **Historical Core & Subsystem Tests (#1 through #924):** Tests covering early CPU interpreters, JIT prototypes, APFS parsers, ELF test harnesses, and low-level MMU experiments. These targets were not executed during the fast Campaign 233 closeout.
- **Campaign 232 Active Tests (#925 through #940, 16 tests):** Validated in `Debug` configuration during Stage 233.23 (36.07 s).
- **Campaign 233 Active Tests (#941 through #955, 15 tests):** Validated in both `RelWithDebInfo` (36.88 s) and `Debug` (386.22 s).

**Correction:** Acknowledged that Campaign 233 executed a focused 31-test subset (Campaign 232 + Campaign 233 targets) rather than the entire 955-test historical repository inventory. Campaign 234 will systematically audit the complete 955-test suite in Stage 234.23.

---

## 4. Terminology Corrections & Overstatement Boundaries

| Prior Terminology in Campaign 233 | Technical Limitation / Reality | Corrected Terminology for Campaign 234 |
| :--- | :--- | :--- |
| "Authenticated WAL journal records" | The journal uses an unkeyed SHA-256 digest (`Sha256::hash_hex`). An unkeyed hash detects disk corruption/torn writes, but cannot defend against an attacker who can write to the journal file and recompute the digest. | **"Integrity-checksummed WAL journal records"** |
| "Independent certification" | Testing was performed using internal test harnesses on MSVC Windows 11 x64 against published RFC/NIST test vectors. No independent third-party auditor or laboratory certified the codebase. | **"Empirical RFC conformance and internal regression verification"** |
| "Proven constant-time behavior" | The implementation uses bitwise arithmetic and avoids data-dependent branches for key material, but CPU microarchitectural effects, cache hierarchy, hyperthreading, and speculative execution were not formally certified. | **"Branch-free / constant-time-intended software implementation"** |
| "Cryptographically unforgeable IPC token" | Capability tokens use Windows CNG 64-bit random values and process generation IDs. They are resistant to brute-force within the guest, but are not asymmetric digital certificates. | **"High-entropy capability handle with anti-replay state"** |

---

## 5. Traceability Matrix: Campaign 233 Claims vs. Concrete Evidence

| Security Domain | Claim | Implementation | Test Target | Configurations Tested | Residual Risk / Known Limitation |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **Ed25519 Signatures** | RFC 8032 Conformance | `src/crypto/ed25519.cpp` | `stage233_3_ed25519_conformance_tests` | RelWithDebInfo, Debug | Custom freestanding pure C++20 implementation; lacks external cross-validation with OpenSSL/libsodium. |
| **Keystore Storage** | DPAPI Secret Protection | `src/security/developer_keystore.cpp` | `stage233_5_keystore_security_tests` | RelWithDebInfo, Debug | Does not protect secrets against code running inside the same logged-in Windows user session. |
| **Trust Model** | Production Trust Enforcement | `src/security/publisher_trust_store.cpp` | `stage233_6_trust_chain_tests`, `stage233_8_key_rotation_revocation_tests` | RelWithDebInfo, Debug | Root certificates are compiled into the application or loaded from VFS, not Windows system cert store. |
| **Path Isolation** | Path Traversal & Device Defense | `src/packaging/canonical_path_validator.cpp` | `stage233_11_hostile_filesystem_tests`, `stage233_20_security_fuzzing_tests` | RelWithDebInfo, Debug | Validation is lexical on input paths. True handle-based confinement at the Win32 API boundary is required (Stage 234.10). |
| **WAL Durability** | Crash Recovery Consistency | `src/packaging/package_installer.cpp` | `stage233_13_wal_durability_tests` | RelWithDebInfo, Debug | Tests simulate process aborts (`inject_fault_at_state`), not physical power loss or OS kernel crashes. |
| **Syscall Memory** | Buffer Safety & Pointer Checks | `src/memory/guest_memory_validator.cpp`, `src/kernel/user_syscall_dispatcher.cpp` | `stage233_15_syscall_adversarial_tests` | RelWithDebInfo, Debug | Tested across Category C syscalls; some legacy Category A/B syscall paths remain to be audited. |
| **Hardware W^X** | Memory Page Permissions | `src/loader/macho_user_loader.cpp`, `src/memory/user_address_space.cpp` | `stage233_19_macho_loader_tests` | RelWithDebInfo, Debug | Validated on software page table descriptors emulating ARM64 EL0 translation tables. |
| **IPC Anti-Replay** | Monotonic Sequence Tracking | `src/kernel/ipc_capability_broker.cpp` | `stage233_16_ipc_anti_replay_tests` | RelWithDebInfo, Debug | Sequence tracking is in-memory; sequence state does not persist across broker recreation. |

---

## 6. Conclusion

With this reconciliation completed, all claims in Campaign 234 are strictly grounded in empirical evidence, with precise terminology and transparent disclosure of system boundaries.
