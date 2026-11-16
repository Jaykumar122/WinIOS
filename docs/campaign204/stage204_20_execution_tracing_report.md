# Stage 204.20: Guest Execution Tracing & Diagnostics Report

**Campaign:** 204 — Public ARM64 Mach-O Application Execution & Darwin Runtime Compatibility  
**Stage:** 204.20 — Guest Execution Tracing & Diagnostics  
**Status:** COMPLETE (Passed in both Release and Debug configurations)  
**Host:** Windows 11 x64 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)

---

## 1. Objectives & Architectural Requirements
The objective of Stage 204.20 is to establish complete runtime observability and post-mortem diagnostic capability for Darwin ARM64 userspace execution without relying on host debugger guesswork or invasive platform hooks:
1. **Thread and Process Attribution:** Every instruction, syscall, memory access, and fault record is tagged with `pid`, `tid`, and mapped image attribution.
2. **Comprehensive Event Streams:**
   - Instruction traces: PC before/after, raw opcode, disasm, EL, SP, NZCV flags, register diffs.
   - Syscall traces: BSD syscalls and Mach traps with arguments, return values, and errno.
   - Memory access traces: Read/Write with addresses, sizes, and values.
   - Memory fault logging: Fault virtual address, FSC status, R/W/X intent, and image attribution.
   - Mach IPC traces: Port names, message IDs, payload sizes, send/receive direction, and status.
   - Thread state transitions: Structured state changes (`NEW -> RUNNING -> BLOCKED -> TERMINATED`).
3. **Bounded Memory Footprint:** Ring buffer eviction maintains bounded memory usage under long-running execution (`max_capacity_`).
4. **Zero-Overhead Toggle:** Fast-path check (`enabled_ == false`) completely bypasses trace serialization and mutex locking.
5. **Deterministic Crash Diagnostics:** Generates self-contained crash diagnostic reports with register dumps, faulting image resolution, and recent instruction history.

---

## 2. Implementation Details
- **Header:** `src/runtime/execution_diagnostics.h`
  - Added `MemoryFaultRecord`, `MachIpcTraceRecord`, `ThreadTransitionRecord`.
  - Added APIs `log_memory_fault`, `log_mach_ipc_op`, `log_thread_transition`.
  - Added `format_crash_diagnostic_report` for human-readable post-mortem diagnostics.
  - Added monotonic `step_index` tracking via `lifecycle_.instructions_retired`.
- **Implementation:** `src/runtime/execution_diagnostics.cpp`
  - Ring buffer capacity trimming across all event vectors.
  - Thread-safe serialization with `std::mutex`.
  - Formatted JSON and CSV export for downstream analysis.

---

## 3. Verification & Test Evidence
- **Test Target:** `stage204_20_execution_tracing_tests`
- **Results:**
  - Lifecycle tracking & image attribution: **PASS**
  - Instruction tracing, disasm & register diffs: **PASS**
  - Syscall & memory operation tracing: **PASS**
  - Memory fault logging & Mach IPC tracking: **PASS**
  - Thread state transition tracking: **PASS**
  - Bounded ring buffer eviction: **PASS**
  - Zero-overhead when disabled: **PASS**
  - Post-mortem crash diagnostic report generation: **PASS**
  - JSON & CSV export: **PASS**
- **Compiler:** MSVC 2026 C++20 with `/W4 /WX /permissive-` (0 warnings, 0 errors).
