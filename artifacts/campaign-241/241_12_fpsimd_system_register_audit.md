# Stage 241.12: FP/SIMD and System-Register Boundary Audit

## 1. Supported Architectural Boundaries
1. **Floating Point**:
   - Scalar IEEE 754 addition, subtraction, multiplication, and division implemented for single (`S0`-`S31`) and double (`D0`-`D31`) precision.
   - NaNs and signed zero division handled gracefully without host CPU crash.
2. **Vector SIMD**:
   - Advanced SIMD (NEON) vector operations are explicitly marked unsupported.
3. **System Registers**:
   - User-mode readable: `NZCV`, `TPIDR_EL0` (TLS base), `CNTVCT_EL0` (virtual timer).
   - Privileged registers (`VBAR_EL1`, `SCTLR_EL1`, `TCR_EL1`) trap when accessed from EL0.
