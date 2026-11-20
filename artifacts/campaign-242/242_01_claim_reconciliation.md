# Stage 242.1: Campaign 240-241 Claim and Evidence Reconciliation

**Campaign**: 242
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Stage 242.1 audited claims and evidence across Campaigns 240 and 241:

| Architectural Domain | Campaign 240 Claim | Campaign 241 Evidence | Campaign 242 Contract Status |
|----------------------|--------------------|------------------------|------------------------------|
| Mach-O Entry Point   | LC_MAIN & LC_UNIXTHREAD | Both parsed | Fully Supported in Freestanding EL0 |
| AAPCS64 General Calls| X0-X7 args, X0 ret | Tested in stage 241.3  | Verified & Documented (16-byte SP) |
| FP / SIMD Floating-Pt| Supported          | Tested in stage 64     | Explicit Support Matrix & Trapping |
| ASID Address Spaces  | Modeled in TTBR0   | Tested in stage 241.10 | Generation Lifecycle & TLB Invalidation |
| Replay Determinism   | CPU registers only | Tested in stage 241.20 | Extended to Memory Hashing & Boundaries |

No unsupported iOS, UIKit, or genuine Apple hardware execution claims are permitted.
