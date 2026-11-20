# Stage 241.1: Campaign 240 Evidence Audit

## 1. Audit Methodology & Scope
Inspected all 25 stage reports (`240_00_baseline.md` through `240_24_final_release_evidence.md`), the root report `WINIOS_CAMPAIGN_240_FINAL_REPORT.md`, and `240_sha256_manifest.txt`.

## 2. Claim, Evidence, and Limitation Matrix

| Major Claim | Implementation Reference | Test Verification Target | Evidence Status | Identified Limitations / Risks |
| :--- | :--- | :--- | :--- | :--- |
| **Freestanding ARM64 Mach-O Workloads** | `guest-sdk/examples/` (4 workloads) | `stage240_15`, `stage240_19` | **VERIFIED** | Freestanding binaries only; no dynamic linking or Darwin dyld support. |
| **Mach-O Header & Segment Parsing** | `src/loader/macho_user_loader.cpp` | `stage240_2` | **VERIFIED** | Limited to `MH_EXECUTE`, `LC_SEGMENT_64`, `LC_MAIN`, and `LC_UNIXTHREAD`. |
| **W^X Memory Enforcement** | `MachoUserLoader::load_image` | `stage240_2`, `stage241_8` | **VERIFIED** | Static load-time enforcement validated; runtime execution checks added in Stage 241.8. |
| **ARM64 Integer & Flag Computation** | `src/cpu/cpu.cpp` | `stage240_5`, `stage241_3` | **VERIFIED** | Bounded subset of user-mode instructions; vector SIMD unsupported. |
| **Exit Code Discrepancy Reconciliation** | `arith_control` (42), `mem_func` (55), `syscall_lifecycle` (77), `multi_app_worker` (88) | `stage240_15`, `stage240_19`, `stage241_17` | **RESOLVED** | Stage 240.15 report documented the first 3 workloads, while `multi_app_worker` was tested in Stage 240.19. Stage 241.17 provides a single unified 4-workload test matrix. |

## 3. Findings
All 26 files in the Campaign 240 manifest matched their cryptographic SHA-256 hashes bit-for-bit.
