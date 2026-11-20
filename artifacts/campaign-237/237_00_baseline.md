# Stage 237.0 — Baseline and Protected Invariants

## 1. Environment and Working Copy Verification

- **Repository Root:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Current HEAD Commit:** `3befe1bcd2997b03bb4af5568e5e2b57cda747be`
- **Expected Baseline Commit:** `f81467bcc800970bc8391c6d7eac0fb419bb5524` (Campaign 235 Closeout)
- **Immediate Predecessor:** Campaign 236 Closeout (`3befe1bcd2997b03bb4af5568e5e2b57cda747be`)
- **Git Status:** Working directory clean (`nothing to commit, working tree clean`)
- **Compiler:** MSVC C++20 (`/W4 /WX /permissive-`)
- **Build System:** CMake 3.31.6
- **Architecture Profile:** ARM64 Mach-O freestanding EL0 (16 KB page granule)
- **Target Profile:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`)

---

## 2. Ancestry & Commit Lineage

```
* 3befe1b - Campaign 236: Full Repository Regression Recovery, Defect Triage & Release Baseline (Stages 236.0-236.24) (HEAD -> campaign-191r)
* f81467b - Campaign 235: Constant-Time Cryptography, Race-Free Filesystem Operations, Durability & Regression Assurance (Stages 235.0-235.24)
* 789ac7b - feat(campaign-234): security assurance closure, handle-based filesystem confinement, WAL durability & cryptographic validation
* 655b43d - feat(campaign-233): cryptographic assurance, hostile filesystem verification, durable recovery & security conformance
```

---

## 3. Protected Invariants Verification

All non-negotiable repository invariants have been verified independently prior to any modification:

| Target Invariant | Expected Value / Condition | Measured Value / Status | Verification Result |
| :--- | :--- | :--- | :--- |
| `D:\WinISO26\WinISO26_README.md` | Nonexistent (`False`) | Nonexistent (`False`) | **PASSED** |
| `D:\WinISO26\WinIOS_README.md` | SHA-256 == `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | **PASSED** |
| `D:\WinISO26\WinIOS\docs\progress.md` | SHA-256 == `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | **PASSED** |
| `D:\WinISO26\Inferno-Research\` | Untouched and uninspected | Exists, untouched (`True`) | **PASSED** |
| Branch Stability | Retain `campaign-191r` | On `campaign-191r` | **PASSED** |
| Toolchain Compliance | MSVC C++20 `/W4 /WX /permissive-` | Configured in CMakeLists.txt | **PASSED** |

---

## 4. Test Inventory Baseline

- Total Registered CTest Targets: **988**
- Range: Test #1 (`memory_tests`) through Test #988 (`stage235_21_security_integration_tests`)
- Inherited Results from Campaign 236:
  - **RelWithDebInfo:** 947 Passed / 40 Failed / 1 Skipped (#383)
  - **Debug:** 943 Passed / 44 Failed / 1 Skipped (#383)
  - Total Active Tests: 987
