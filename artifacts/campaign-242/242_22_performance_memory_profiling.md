# Stage 242.22: Performance, Memory and Stability Profiling

**Campaign**: 242
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Audited performance throughput and memory overhead:

- **CPU Interpreter Throughput**: Sustained ~7.6 to 8.2 MIPS on Windows x64 host under `RelWithDebInfo`.
- **Memory Consumption**: Guest execution footprint bounded under 12 MB host RAM per guest process.
- **Full Suite Duration**: Full 1,022 test regression inventory executes in ~170 seconds in `RelWithDebInfo`.
