# Stage 236.0: Baseline Resolution & Repository Safety Audit

## Executive Summary

Stage 236.0 establishes the verified baseline for WinIOS Campaign 236 ("Full Repository Regression Recovery, Legacy Failure Triage, Security Claim Verification & Release Baseline"). All protected repository invariants, branch integrity, HEAD commit hashes, and the complete registered CTest inventory (988 tests) were verified before any source code modifications.

---

## 1. Git Repository State & Verification

| Property | Expected Specification | Verified Runtime State | Match Status |
| :--- | :--- | :--- | :---: |
| **Repository Root** | `D:\WinISO26\WinIOS\` | `D:\WinISO26\WinIOS\` | **EXACT MATCH** |
| **Active Branch** | `campaign-191r` | `campaign-191r` | **EXACT MATCH** |
| **Current HEAD Hash** | `f81467bcc800970bc8391c6d7eac0fb419bb5524` | `f81467bcc800970bc8391c6d7eac0fb419bb5524` | **EXACT MATCH** |
| **Campaign 235 Baseline**| `789ac7bccfec6f0277b30d885350f6f8f4e18ee5` | `789ac7bccfec6f0277b30d885350f6f8f4e18ee5` | **EXACT MATCH** |
| **Working Tree Status** | Clean (no uncommitted tracked or untracked changes) | `nothing to commit, working tree clean` | **VERIFIED CLEAN** |

### Recent Git Commit History
```text
f81467b Campaign 235: Constant-Time Cryptography, Race-Free Filesystem Operations, Durability & Regression Assurance (Stages 235.0-235.24)
789ac7b feat(campaign-234): security assurance closure, handle-based filesystem confinement, WAL durability & cryptographic validation
655b43d feat(campaign-233): cryptographic assurance, hostile filesystem verification, durable recovery & security conformance
429e3a8 feat(campaign-232): asymmetric publisher authentication, secure package distribution, filesystem isolation & runtime security verification
076fbac feat(campaign-231): package authenticity, transactional runtime, ABI conformance and reproducible tooling
```

---

## 2. Non-Negotiable Invariants Audit

| Invariant Item | Required Value / Condition | Measured Value | Conformance |
| :--- | :--- | :--- | :---: |
| `D:\WinISO26\WinISO26_README.md` | Must remain nonexistent | `False` (File does not exist) | **PASS** |
| `D:\WinISO26\WinIOS_README.md` SHA-256 | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **PASS** |
| `D:\WinISO26\WinIOS\docs\progress.md` SHA-256 | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **PASS** |
| `D:\WinISO26\Inferno-Research\` | Untouched directory | Untouched (LastWriteTime: 11/3/2026 11:14 AM) | **PASS** |
| Local Commit Discipline | Strictly local commits on `campaign-191r`; no push/reset/clean/rebase | Confirmed; no remote push executed | **PASS** |
| Toolchain & Warnings | MSVC C++20 `/W4 /WX /permissive-` | 0 warnings, 0 errors | **PASS** |

---

## 3. CTest Test Inventory Enumeration

CTest test enumeration was executed across both supported build configurations:

- **RelWithDebInfo Configuration Registered Test Count**: **988 tests**
- **Debug Configuration Registered Test Count**: **988 tests**

### Inventory Distribution
- **Core Kernel, MMU, Exception & Architecture**: Tests #1 – #58, #60 – #80
- **Userspace Mach-O, DYLD, PAC & Subsystems**: Tests #81 – #120, #140 – #160
- **Campaign 211–227 Historical Stages (UI, Schedulers, IPC)**: Tests #640 – #870
- **Campaign 228–230 Application Ecosystem & SDK v2.3**: Tests #871 – #904
- **Campaign 231 Security & Package Authenticity (WIOSPKG3)**: Tests #905 – #924
- **Campaign 232 Asymmetric Ed25519 Signatures & WIOSAUTH v2**: Tests #925 – #940
- **Campaign 233 Cryptographic Assurance & WAL Durability**: Tests #941 – #955
- **Campaign 234 Security Assurance Closure & Filesystem Confinement**: Tests #956 – #973
- **Campaign 235 Constant-Time Crypto, Race-Free FS & Regression**: Tests #974 – #988

---

## 4. Operational Safety Baseline

1. Build directory `D:\WinISO26\WinIOS\build` contains intact prebuilt binaries for all 988 test targets in both `RelWithDebInfo` and `Debug`.
2. Existing test logs from Campaign 235 are preserved.
3. No code modifications will be performed until Stage 236.1 and Stage 236.2 audit and classify every existing failure with empirical evidence.
