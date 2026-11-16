# Campaign 205 Known Limitations and Future Research

## 1. Technical Truth Disclosure
WinIOS maintains absolute transparency regarding what is synthetic, what is genuinely emulated, and what is currently blocked by proprietary Apple silicon.

## 2. Capability Matrix by Level
| Execution Level | Description | Status in WinIOS | Primary Blocker / Reason |
| :--- | :--- | :--- | :--- |
| **Level 1** | ARM64 Virtual Boot Services | **VERIFIED OPERATIONAL** | N/A — EL1/EL2, GICv3, MMU 16KB, timers operational. |
| **Level 2** | Genuine Kernel Entry | **VERIFIED OPERATIONAL** | N/A — Mach-O parser, mapper, boot_args Rev 2, DT handoff complete. |
| **Level 3** | Early Darwin/XNU Initialization | **PARTIALLY OPERATIONAL** | Halts at physical SoC hardware boundary (AICv2 MMIO / Apple SPRs). |
| **Level 4** | Full Darwin Kernel Services | **BLOCKED BY SILICON** | Apple proprietary Power Manager (PMGR), AICv2, DART IOMMU, SPRs. |
| **Level 5** | Production iOS Boot | **BLOCKED BY PROPRIETARY SILICON** | Apple SecureROM signature checks, SEP hardware enclave keys, encrypted FairPlay kernelcache filesets. |

## 3. Detailed Hardware Blockers
1. **Apple Interrupt Controller (AICv2):**
   - Public ARM64 architecture specifies ARM GIC (GICv3/GICv4).
   - Apple Silicon (A16 Bionic / T8120) utilizes a proprietary interrupt controller architecture (AICv2) located at `0x23B100000`, using custom memory-mapped registers and proprietary CPU system registers (`SYS_APL_AIC_*`).
2. **Secure Enclave Processor (SEP):**
   - Production iOS devices verify hardware keys using a dedicated, physically isolated Secure Enclave running proprietary sepos firmware. Without hardware cryptographic keys, genuine iOS production kernelcaches cannot decrypt or authenticate kernel extensions.
3. **Apple Power Management (PMGR):**
   - Early XNU clocks and power domains depend on undocumented Apple PMGR register arrays to enable CPU cores and peripheral clocks.
