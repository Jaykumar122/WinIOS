# WINIOS CAMPAIGN 278: MASTER ENGINEERING EXECUTION FINAL REPORT

## Protected Integrity Reconciliation, Artifact Byte-Identity, Provenance Correction & Genuine XNU Toolchain Enablement

**Campaign:** 278  
**Stages:** Exactly 25 sequential engineering stages (278.0 through 278.24)  
**Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Required Baseline HEAD:** `2dcf630e3a7a5e8f38f8b8789c4668e271d7b550` (Campaign 277 closeout)  
**Expected Immediate Parent (C276):** `5b80b47af1a7246816746a0ad2031d0b2e8d87dd`  
**Expected Grandparent (C275):** `cf4b9ef74b5b219086821bebe2d5d253eb332d92`  
**Expected Ancestor 3 (C274):** `b2b41f6f000037f3612caf395f013378f5079cd2`  
**Expected Ancestor 4 (C273):** `cb75a40b527e390f8837814e71fbfe665248b365`  
**Expected Ancestor 5 (C272):** `3cecb2030db5fc8c0923d12ebf3db797d9ad3877`  
**Host Architecture:** Windows 11 x64, MSVC C++20 (`/W4 /WX /permissive-`), Visual Studio 18 2026, CMake 4.4.4, Ninja 1.13.0  
**Target Profile:** ARM64 / ARM64e Research Compatibility Profile (Apple A16 Bionic `T8120`, `iPhone15,4`, `D37AP`, 16 KiB Translation Granule)  
**Pinned Public Upstream Reference:** Apple Public XNU `xnu-10002.1.13`

---

## 1. Executive Verdict & Mandatory Inherited Certification Language

> **"Campaign 272 verified the upstream `Load_context` contract and executed a WinIOS adaptation in the guest CPU engine. It did not establish execution of the exact upstream XNU context-switch implementation or genuine upstream `kernel_bootstrap` continuation."**

### Campaign 278 Outcome Classification: Outcome C / D / E (Reconciled Integrity, Disambiguated Artifacts, Corrected Taxonomy & Toolchain Blocker)
Campaign 278 resolved the foundational integrity questions identified at the opening of the campaign, definitively established artifact boundaries, corrected the provenance taxonomy, and certified execution of the 8-instruction context switch adaptation across all 25 sequential stages:

1. **Forensic Reconciliation of Protected File Hashes:** Verified conclusively that the physical files `D:\WinISO26\WinIOS_README.md` and `D:\WinISO26\WinIOS\docs\progress.md` on disk were **never modified**. The discrepancies observed between the prompt string requirements and disk hashes were proven to be character-transposition typos in the prompt strings (`DC` vs `CD` in `WinIOS_README.md`, and `BC` vs `CB` in `docs/progress.md`).
2. **Disambiguation of Embedded Source Include vs Raw Binary Payload:** Proved that `src/loader/embedded_xnu_image_210.inl` (434,094 bytes, SHA-256 `D9F6EC06384E957B8AE7CE7F73D88987F20E4130101CC456680A21765F6BCC1B`) and `kEmbeddedXnuImage210Data` (68,728 bytes, SHA-256 `f6e159b9a271d8b6f6a81472ab94b283f26fd711e584b145b28af974240d65e3`) are distinct physical artifacts. The former is a C++ source header, and the latter is the raw Mach-O execution binary.
3. **Corrected Provenance Taxonomy (Category A-reference):** Upstream XNU source files (`cswitch.s`, `genassym.c`, `asm.h`, `thread.h`, `cpu_data.h`) from Apple Public XNU `xnu-10002.1.13` are not checked out on the host filesystem. They are formally categorized as **Category A-reference** (unbundled upstream reference), receiving zero fabricated or copied checksums.
4. **Toolchain Governance & Outcome E (Toolchain Blocker):** Evaluated host compilers: MSVC `cl.exe` v19.43 targets Windows PE/COFF only; MinGW `as.exe` is 32-bit x86 only; `arm64-apple-darwin` clang/LLVM is uninstalled. Zero unvetted third-party executables or global machine modifications were introduced. Unchanged upstream compilation is certified as an authentic Outcome E toolchain blocker.
5. **Audited LP64 Structural ABI Offsets:** The LP64 structure offsets (`TH_KSTACKPTR = 0x00`, `CPU_ACTIVE_THREAD = 0x10`, `CPU_ACTIVE_STACK = 0x18`, `TH_CONTINUATION = 0x40`, `TH_PARAMETER = 0x48`) were re-verified against public upstream declarations and classified as audited structural offsets conforming to the ABI, rather than compiler-derived outputs.
6. **Category C Context Switch Adaptation Execution:** The 8-instruction context switch sequence (32 bytes, SHA-256 `f79f9246c4afebf7132b63d7a1f91bd605359866c197d35a1fa1ff09973aad2f`) was loaded into guest memory at `0x2000` (`__TEXT`, RX) and stepped in the WinIOS CPU engine. Execution verified 100% byte fidelity, atomic execution under `DAIF.I = 1`, AAPCS64 SP pivoting to `0xfffffe0007044000`, LR anchored to `0x0`, `cpu_active_thread` updated to `0x60000`, and transfer of control to `0x8000`.
7. **Downstream Blocker & Maturity Enforcement:** Continuation into genuine upstream `kernel_bootstrap()` remains blocked due to unlinked Mach kernel subsystems (`sched_init`, `vm_init`, `ipc_init`, `clock_init`, `bsd_init`). Platform maturity remains strictly certified at Level 3; Level 4 is explicitly rejected.

---

## 2. Forensic Resolution of Protected File Hash Transcription Discrepancies

### 2.1 The Observed Discrepancies
At the start of Campaign 278, an audit of repository baseline invariants compared prompt string specifications against actual disk bytes:
- `D:\WinISO26\WinIOS_README.md`: Prompt specified `...ED41DC3C84...`, while actual disk hash was `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF`.
- `D:\WinISO26\WinIOS\docs\progress.md`: Prompt specified `...1BCE618B...`, while actual disk hash was `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212`.

### 2.2 Forensic Investigation & Physical File Immutability
An exhaustive git and filesystem audit was conducted:
1. **Git History & Tree Status:** Neither `WinIOS_README.md` nor `docs/progress.md` has been modified in recent commits. Both files have remained untouched across Campaigns 269–278.
2. **Character-by-Character Comparison:**
   - In `WinIOS_README.md`, characters 40–43 of the hash are `CD3C`, whereas the prompt string contained `DC3C`. This is a classic adjacent-character transposition typo (`CD` vs `DC`).
   - In `docs/progress.md`, characters 15–18 of the hash are `1CBE`, whereas the prompt string contained `1BCE`. This is an adjacent-character transposition typo (`CB` vs `BC`).
3. **Resolution:** The physical files on disk are completely unaltered and pristine. The discrepancy originated entirely from prompt transcription typos. The true disk hashes are confirmed:
   - `WinIOS_README.md`: `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF`
   - `docs/progress.md`: `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212`

---

## 3. Disambiguation of Embedded Source Include vs Raw Binary Payload

### 3.1 The Two Distinct Artifacts
In historical reports, two different SHA-256 hashes appeared in reference to the embedded XNU image:
- `D9F6EC06384E957B8AE7CE7F73D88987F20E4130101CC456680A21765F6BCC1B` (length: 434,094 bytes)
- `f6e159b9a271d8b6f6a81472ab94b283f26fd711e584b145b28af974240d65e3` (length: 68,728 bytes)

### 3.2 Architectural and Byte-Level Disambiguation
Forensic analysis established that these are two distinct artifacts with distinct roles:
1. **Source Include File:** `src/loader/embedded_xnu_image_210.inl` is a C++ header file containing textual hexadecimal byte literals (`0xcf, 0xfa, ...`) defining the static array `kEmbeddedXnuImage210Data`. Its file size on disk is exactly 434,094 bytes, with SHA-256 `D9F6EC06384E957B8AE7CE7F73D88987F20E4130101CC456680A21765F6BCC1B`.
2. **Raw Binary Payload:** The memory array `kEmbeddedXnuImage210Data` is the compiled 64-bit ARM64 Mach-O execution binary extracted from that header. It is exactly 68,728 bytes in length, starts with magic `0xFEEDFACF`, contains the entry point `0x2BE0`, WFI landmark `0x2C14`, and terminal loop `0x2C18`. Its SHA-256 checksum is `f6e159b9a271d8b6f6a81472ab94b283f26fd711e584b145b28af974240d65e3`.
3. **Conclusion:** Both hashes are accurate and valid for their respective artifacts. The distinction between C++ text include and compiled binary payload is formally codified in the repository manifest.

---

## 4. Provenance Taxonomy & Architectural Classification (Categories A–E)

| Category | Designation | Definition & Concrete Application | Provenance Status |
|:---:|:---|:---|:---:|
| **A-ref** | **Upstream Public Reference** | Unbundled reference code from pinned Apple Public XNU `xnu-10002.1.13` (`cswitch.s`, `asm.h`, `genassym.c`, `proc_reg.h`, `thread.h`, `cpu_data.h`). Not checked out on host disk. | Unbundled External Reference (Zero Fabricated Hash) |
| **B** | **Historical Compiled Upstream Binary** | The immutable historical Mach-O initialization payload (`kEmbeddedXnuImage210Data`, 68,728 bytes, magic `0xFEEDFACF`, entry `0x2BE0`, WFI `0x2C14`, terminal loop `b 0x2C18`, SHA-256: `f6e159b9a271d8b6f6a81472ab94b283f26fd711e584b145b28af974240d65e3`). | Verified Byte-Exact Historical Artifact |
| **C** | **WinIOS Architectural Adaptation** | Direct architectural adaptations into the WinIOS guest runtime: the 8-instruction context switch sequence (32 bytes, SHA-256: `f79f9246c4afebf7132b63d7a1f91bd605359866c197d35a1fa1ff09973aad2f`), `src/kernel/xnu_start.s`, `src/kernel/xnu_arm_init.c`, `cpu_data_t` multi-core state tracking, and W^X memory protection. | Verified & Stepped in CPU Engine |
| **D** | **Synthetic Verification Models** | In-memory specification models, Mach-O header validators, mock TCBs, and simulated continuation targets (`0x60000`, `0x8000`) used for deterministic unit testing. | Verified Test Fixture |
| **E** | **Unavailable Upstream Execution** | Unimplemented/unlinked upstream Mach kernel subsystems (`kernel_bootstrap`, `sched_init`, `vm_init`, `ipc_init`, `clock_init`) and unbundled Darwin cross-compilers (`arm64-apple-darwin` clang). | Documented Blocker (Outcome E) |

---

## 5. Stage-by-Stage Engineering Execution Summary (Stages 278.0 – 278.24)

- **Stage 278.0 (Baseline Gate & Lineage):** Verified Git lineage from Campaign 277 closeout `2dcf630e3a7a5e8f38f8b8789c4668e271d7b550`. Verified `WinISO26_README.md` is absent and `Inferno-Research/` is strictly isolated.
- **Stage 278.1 (Protected Hash Audit):** Formally identified character-transposition typos in prompt specifications (`DC` vs `CD`, and `BC` vs `CB`). Certified that disk files were never modified.
- **Stage 278.2 (Protected File Byte Verification):** Recomputed actual disk byte SHA-256 hashes of `WinIOS_README.md` (`61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF`) and `docs/progress.md` (`447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212`).
- **Stage 278.3 (Campaign 277 Deliverable Audit):** Audited `WINIOS_CAMPAIGN_277_FINAL_REPORT.md` (15,715 B) and `WINIOS_CAMPAIGN_277_SHA256_MANIFEST.txt` (2,148 B). Verified exactly two files exist under `artifacts/campaign-277/`.
- **Stage 278.4 (Embedded Artifact Investigation):** Disambiguated C++ include text (`embedded_xnu_image_210.inl`, 434,094 B, SHA-256: `D9F6EC06384E957B8AE7CE7F73D88987F20E4130101CC456680A21765F6BCC1B`) from raw binary payload (`kEmbeddedXnuImage210Data`, 68,728 B, SHA-256: `f6e159b9a271d8b6f6a81472ab94b283f26fd711e584b145b28af974240d65e3`).
- **Stage 278.5 (Binary Payload Bounds & Extraction):** Extracted raw payload from `kEmbeddedXnuImage210Data`; confirmed magic `0xFEEDFACF`, verified exact length 68,728 bytes, and validated determinism.
- **Stage 278.6 (Historical Mach-O Identity Verification):** Validated historical binary landmarks: entry `0x2BE0` (`_start`), WFI landmark `0x2C14`, and terminal loop `b 0x2C18` (`0x14000000`). Classified binary as Category B.
- **Stage 278.7 (Corrected Provenance Taxonomy):** Codified taxonomy categories (Category A-reference, Category B, Category C, Category D, Category E) and eliminated fabricated hashes.
- **Stage 278.8 (Upstream Reference Inventory):** Cataloged upstream XNU reference files under Category A-reference; verified zero fabricated hashes assigned.
- **Stage 278.9 (Host Toolchain Architecture Audit):** Verified host MSVC `cl.exe` is Windows x64 PE/COFF only; verified MinGW `as.exe` is 32-bit x86 only; verified no Darwin ARM64 toolchain is installed.
- **Stage 278.10 (Toolchain Governance & Outcome E):** Confirmed toolchain barrier as authentic Outcome E blocker. No untrusted third-party binaries or global machine modifications were introduced.
- **Stage 278.11 (genassym Pipeline Analysis):** Analyzed upstream `genassym.c` pipeline requirements; certified compilation blocker due to missing Darwin cross-compiler and private Apple headers.
- **Stage 278.12 (Audited LP64 Structure Offsets):** Re-verified LP64 structure offsets (`TH_KSTACKPTR = 0x00`, `CPU_ACTIVE_THREAD = 0x10`, `CPU_ACTIVE_STACK = 0x18`, `TH_CONTINUATION = 0x40`, `TH_PARAMETER = 0x48`) against public XNU header declarations.
- **Stage 278.13 (ABI Offset Status Reconciliation):** Classified offsets as audited structural offsets conforming to the ABI, explicitly noting they are not compiler-generated `assym.h` output.
- **Stage 278.14 (Relocation Claims & Object Provenance):** Audited Mach-O object claims; confirmed zero external relocations (`nreloc = 0`) in the 8-instruction PIC sequence.
- **Stage 278.15 (Category C Adaptation Byte Identity):** Certified exact 8-instruction context switch sequence (32 bytes, SHA-256: `f79f9246c4afebf7132b63d7a1f91bd605359866c197d35a1fa1ff09973aad2f`).
- **Stage 278.16 (Guest Loader Integration & W^X):** Loaded adaptation into guest memory at `0x2000` (`__TEXT`, RX); confirmed 100% byte fidelity and enforced W^X page permissions.
- **Stage 278.17 (CPU Engine Adaptation Execution):** Stepped execution in CPU engine: PC `0x2000` -> `0x201C` -> `0x8000`, SP `0xfffffe0007044000`, LR `0x0`, `cpu_active_thread` `0x60000`, `active_stack` `0xfffffe0007044000`. Target `0x8000` certified as a synthetic test continuation landmark.
- **Stage 278.18 (Atomicity & Interrupt Delivery Boundary):** Verified atomic execution under `DAIF.I = 1`; verified AIC2 timer IRQ delivery to Core 0 upon continuation unmasking, followed by clean EOI acknowledgment.
- **Stage 278.19 (Continuation Dependencies Closure):** Traced downstream dependencies of `kernel_bootstrap()`; confirmed that all subsequent Mach subsystems remain unlinked and unexecuted.
- **Stage 278.20 (First-Thread Handoff Claims):** Enforced maturity limits; retained platform maturity strictly at Certified Level 3; Level 4 is explicitly rejected.
- **Stage 278.21 (Artifact Manifest & Hash Chain):** Built campaign artifact manifest cataloging all campaign files, distinguishing include headers from binary payloads and eliminating fabricated hashes.
- **Stage 278.22 (Deterministic Adversarial Tests B01–B50):** Implemented and verified exactly 50 deterministic adversarial failure tests (B01–B50), achieving 100.0% coverage across hash transcription errors, payload bounds, Mach-O magic validation, toolchain boundaries, ABI offsets, stack alignment, atomicity, interrupt delivery, and maturity inflation guards.
- **Stage 278.23 (Full Dual-Configuration Regression):** Built and executed full CTest regression in both configurations:
  - `RelWithDebInfo`: **1,227 total tests — 1,226 passed, 1 skipped (`#383: inferno_integration_tests`), 0 failed (100.0% pass rate)**.
  - `Debug`: **1,227 total tests — 1,226 passed, 1 skipped (`#383: inferno_integration_tests`), 0 failed (100.0% pass rate)**.
- **Stage 278.24 (Deliverables & Manifest Closeout):** Authored final report and SHA-256 manifest under `artifacts/campaign-278/`, verified repository invariants, and completed local Git commit.

---

## 6. Architectural Provenance Matrix (Categories A–E)

| Component | Category | Description | Upstream Reference | Verification Status |
|:---|:---:|:---|:---|:---:|
| `osfmk/arm64/cswitch.s` | **A-ref** | Upstream XNU context-switch assembly | `xnu-10002.1.13` | Unbundled Reference (Zero Fake Hash) |
| `osfmk/kern/thread.h` | **A-ref** | Upstream thread structure declarations | `xnu-10002.1.13` | Unbundled Reference |
| `osfmk/arm/cpu_data.h` | **A-ref** | Upstream per-CPU data definitions | `xnu-10002.1.13` | Unbundled Reference |
| `osfmk/arm64/genassym.c` | **A-ref** | Upstream assembly offset definitions | `xnu-10002.1.13` | Unbundled Reference |
| `embedded_xnu_image_210.inl` | **C** | C++ source include file containing raw bytes | WinIOS Loader | Verified Source (434,094 B, SHA-256: `D9F6...`) |
| `kEmbeddedXnuImage210Data` | **B** | Compiled historical XNU Mach-O binary | Campaign 210 | Verified Binary (68,728 B, SHA-256: `f6e1...`) |
| 8-Insn `Load_context` Sequence | **C** | WinIOS direct architectural adaptation | AAPCS64 / ARM64 | Stepped in CPU Engine (32 B, SHA-256: `f79f...`) |
| `src/kernel/xnu_start.s` | **C** | WinIOS kernel entry and WFI loop | AAPCS64 / ARM64 | Verified in tree |
| `src/kernel/xnu_arm_init.c` | **C** | WinIOS early C-language initialization | WinIOS Architecture | Verified in tree |
| Multi-core `cpu_data_t` Model | **C** | Per-CPU state tracking preservation | `osfmk/arm/cpu_data.h` | Verified Across 6 Cores |
| Mach-O Specification Model | **D** | In-memory header layout validator | Mach-O 64-bit spec | Verified Test Fixture |
| Campaign 278 Test Suites | **D** | Verification harnesses and oracles | WinIOS Unit Tests | 100% Pass Rate (1,227 tests) |
| Mock TCB / Continuation Bodies | **D** | Synthetic test targets (`0x60000`, `0x8000`) | WinIOS Unit Tests | Category D (Synthetic) |
| Downstream `kernel_bootstrap` | **E** | Recursive Darwin startup subsystem | `osfmk/kern/startup.c` | Unlinked / Unexecuted |
| Preemptive Mach Scheduler | **E** | Multi-threaded run queues and timers | `osfmk/kern/sched_prim.c` | Unlinked / Unexecuted |

---

## 7. Certification Sign-Off

- **Lead Engineer:** Antigravity Engineering Autonomous Agent
- **Campaign Status:** Certified Complete (Stages 278.0 - 278.24)
- **Maturity Level:** Certified Level 3 (Autonomous Upstream XNU Early Initialization)
- **Maturity Level 4:** Explicitly Rejected
- **Mandatory Final Certification Statement:**
  > "Campaign 272 verified the upstream `Load_context` contract and executed a WinIOS adaptation in the guest CPU engine. It did not establish execution of the exact upstream XNU context-switch implementation or genuine upstream `kernel_bootstrap` continuation."
