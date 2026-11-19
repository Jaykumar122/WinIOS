# Stage 232.1: Independent Campaign 231 Security Audit & Finding Register

## 1. Executive Summary

An independent, rigorous source-level security audit was conducted on the implementation deliverables of Campaign 231. While Campaign 231 established foundational building blocks (CSPRNG integration, generational handle tracking, crash-consistent directory staging, and Category C ABI v2.4), critical security vulnerabilities and architectural gaps were identified.

Most notably, **Campaign 231 relied on symmetric HMAC-SHA256 secrets for publisher authentication**. Because HMAC requires both the signer and the verifier to possess the same secret key, any entity authorized to verify an application package (such as the client runtime or developer CLI) has the cryptographic capability to forge valid signatures for that publisher. Furthermore, trailer metadata was not bound to the signature domain, filesystem root containment relied on simple string prefixing without reparse-point defense, and IPC anti-replay checks were bypassed when sequence numbers were omitted.

---

## 2. Finding Register

| Finding ID | Severity | Affected Component | Summary & Impact | Remediation Plan |
|---|---|---|---|---|
| **SEC-232-01** | **HIGH** | `PublisherTrustStore` (`publisher_trust_store.h/.cpp`) | **Symmetric Key Sharing in Verification Engine**: HMAC-SHA256 requires verification engines to possess the signing secret. Leaking or distributing the secret for verification allows signature forgery. | Implement asymmetric digital signatures (Ed25519) separating private signing keys from public verification keys. |
| **SEC-232-02** | **HIGH** | `AppPackage` & `PublisherTrustStore` | **Unbound Authenticated Trailer Metadata**: The signature hash domain only covered the raw package container body. Trailer metadata (version, algorithm, publisher ID, timestamps) was unhashed, allowing parameter substitution. | Define WIOSAUTH v2 trailer format where a canonical metadata block is cryptographically bound into the signed payload. |
| **SEC-232-03** | **HIGH** | `CanonicalPathValidator` (`canonical_path_validator.cpp`) | **Lexical String-Prefix Jail Containment**: `is_strictly_inside_root` relied solely on `rfind` string prefixing. Windows NTFS junctions, symlinks, and casing differences can redirect filesystem I/O outside sandbox roots. | Implement handle-based filesystem canonicalization (`GetFinalPathNameByHandleW`) and explicit reparse-point rejection. |
| **SEC-232-04** | **HIGH** | `IpcCapabilityBroker` (`ipc_capability_broker.h`) | **Anti-Replay Bypass via Zero Sequence Number**: `send_message` only enforced monotonic sequence checking if `sequence_num > 0`. Transmitting `0` allowed arbitrary message replay. | Enforce mandatory non-zero, monotonic 64-bit sequence numbers for all capability-bound IPC transmissions. |
| **SEC-232-05** | **MEDIUM** | `UserSyscallDispatcher` (`user_syscall_dispatcher.cpp`) | **Unbounded Null-Terminated String Reads**: Guest string arguments read byte-by-byte until `\0` could run into unmapped pages, triggering unhandled memory faults instead of `EFAULT`. | Implement bounded `copyout_string` with explicit maximum length and page-range pre-validation. |
| **SEC-232-06** | **MEDIUM** | `PackageInstaller` (`package_installer.h/.cpp`) | **Lack of Persistent Transaction Journal**: Recovery relied solely on inspecting leftover `.staging` directories on boot without an explicit transaction state journal. | Define explicit 11-state installer transaction model with on-disk state journal for crash recovery. |
| **SEC-232-07** | **MEDIUM** | `winios-cli` / Key Storage | **Plaintext Private Key Persistence**: Keys in fixtures and storage were held in plaintext bytes without OS-level cryptographic protection (DPAPI/CNG). | Protect private signing keys on Windows using CNG/DPAPI and isolate developer keys from runtime trust stores. |
| **SEC-232-08** | **LOW** | `PublisherTrustStore` (`publisher_trust_store.h`) | **Missing Algorithm Downgrade Rejection**: The trust store did not reject signature algorithm mismatches between key definitions and trailer declarations. | Enforce strict algorithm pinning and explicit rejection of legacy HMAC downgrades. |

---

## 3. Audit of Campaign 231 Claims vs Observed Implementation

| Feature / Claim | Campaign 231 Documentation Claim | Actual Implementation Status | Audit Finding |
|---|---|---|---|
| **Publisher Authenticity** | "Cryptographically authenticated packages via digital signatures" | **Partially Proven**: Implemented using symmetric HMAC-SHA256, which provides integrity and message authentication under shared secrets, but **not** asymmetric publisher authenticity. | Must transition to Ed25519 public-key cryptography. |
| **CSPRNG Identity Tokens** | "128-bit CSPRNG instance tokens sourced from Windows CNG" | **Proven**: `BCryptGenRandom` properly utilized in `security_utils.cpp` for token and salt generation. | Maintained for Campaign 232. |
| **Generational Handle Isolation** | "Eliminated PID recycling and stale handle privilege leakage" | **Proven**: Generational handles verified in `ProcessIdentityManager`, `IpcCapabilityBroker`, and `DocumentRegistry`. | Proven, but IPC replay check needed hardening (SEC-232-04). |
| **Crash-Consistent Installer** | "Transactional three-phase installation with automated rollback" | **Partially Proven**: Staging and directory rename implemented; however, lacks persistent state journal and crash-injection verification. | Upgrade to Transactional Installer v2 with state journal. |
| **Filesystem Jail Containment** | "Canonical path validation prevents all sandbox escapes" | **Partially Proven**: Rejects `..`, `:`, and drive letters lexically, but vulnerable to Windows NTFS reparse points and junctions. | Harden with Win32 file-handle inspection. |
| **Category C ABI v2.4 Conformance** | "Syscalls 581..593 struct layout and boundary safety verified" | **Proven**: Struct offsets, sizes, and alignment match between host and guest. String boundary checks require tightening. | Compile-time ABI assertions to be expanded. |
