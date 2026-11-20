# Stage 240.8: Floating-Point and SIMD Execution

## 1. FP State and Semantics
1. **Register Bank**:
   - 32 128-bit SIMD/FP registers (`V0` - `V31`).
   - Scalar accessors `S0` - `S31` (32-bit single) and `D0` - `D31` (64-bit double).
   - Control register `FPCR` and status register `FPSR`.
2. **Arithmetic & Edge Cases**:
   - Conforms to IEEE 754-2008 standards for addition, subtraction, multiplication, and division.
   - Quiet NaN and Signaling NaN propagation tested.
   - Zero division produces signed infinity without host CPU traps.
3. **Architecture Boundaries**:
   - Vectorized NEON SIMD operations are explicitly documented as unsupported and trigger graceful exception handling.
