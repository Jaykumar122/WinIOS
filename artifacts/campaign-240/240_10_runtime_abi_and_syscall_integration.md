# Stage 240.10: Runtime ABI and Syscall Integration

## 1. Syscall Dispatch & ABI Versioning
1. **Dispatch Flow**:
   - Register `X16` carries syscall ID (or `X0` in FastABI).
   - Arguments passed in `X0` - `X5`.
   - Result returned in `X0`, error code returned in `X0` (negative errno) or carry flag.
2. **Category C ABI Negotiation (Syscall 507)**:
   - Request: `X16 = 507`, `X0 = requested_version` (`0x00010100`).
   - Response: Negotiated version returned in `X0`.
   - Downgrade protection prevents downgrading negotiated capabilities.
3. **Syscall Contract Validation**:
   - Integrated with `SyscallRegistry`.
   - Buffer memory ranges checked for guest address bounds before kernel read/write.
