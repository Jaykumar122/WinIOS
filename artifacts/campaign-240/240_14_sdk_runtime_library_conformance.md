# Stage 240.14: SDK Runtime Library Conformance

## 1. Freestanding Runtime Services
1. **Entry Point Assembly (`_start`)**:
   - Sets up initial frame pointer `X29 = 0`.
   - Calls `main(0, NULL)`.
   - Passes return value of `main` to `winios_exit()`.
2. **System Call Wrappers**:
   - `winios_syscall0` through `winios_syscall6` implemented via inline assembly `svc #0`.
3. **Standard Memory Primitives**:
   - `winios_memcpy`: Byte-aligned memory copy.
   - `winios_memset`: Deterministic buffer filling.
   - `winios_memcmp`: Constant-time memory comparison.
4. **Lifecycle Wrappers**:
   - `winios_exit(status)`
   - `winios_getpid()`
   - `winios_getuid()`
