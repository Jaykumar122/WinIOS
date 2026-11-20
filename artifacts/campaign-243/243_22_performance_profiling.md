# Stage 243.22: Performance and Resource Profiling

**Campaign**: 243
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Profiled interpreter and replay execution costs:

- **Throughput**: Maintained ~7.7 to 8.3 MIPS sustained instruction throughput.
- **Replay V2 Overhead**: Dual memory hashing and syscall event logging introduced less than 3.5% overhead relative to raw execution.
- **Memory Footprint**: Average process memory footprint remained strictly bounded under 12 MB host RAM.
