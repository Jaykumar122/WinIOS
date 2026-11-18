# WinIOS Campaign 224 — Stage 224.16 Crash Reporting & Diagnostic Records

## 1. Structured Crash Records
When an ARM64 guest application faults, aborts, or violates sandbox capabilities, `AppSupervisor::record_crash` captures an immutable diagnostic record:

```cpp
struct CrashRecord {
    std::uint32_t pid;                     // Faulting Process ID
    std::string   bundle_id;               // Owning Application Bundle ID
    int           exit_code;               // Non-zero termination exit code
    CrashCategory category;                // Detailed taxonomy category
    std::uint64_t guest_pc;                // ARM64 Program Counter at fault
    std::uint64_t instructions_retired;    // Total retired instructions
    std::uint64_t timestamp_us;            // Monotonic timestamp of failure
    std::string   last_lifecycle_state;    // e.g. "Foreground", "Starting"
    bool          resources_cleaned_up;    // True if windows & IPC reclaimed
};
```

---

## 2. Failure Taxonomy
| Category | Value | Description |
| :--- | :---: | :--- |
| `IllegalInstruction` | 1 | Undefined or privileged ARM64 opcode in EL0 |
| `MemoryFault` | 2 | Unmapped VA, permission fault, or unaligned access |
| `Abort` | 3 | Explicit application abort or assertion failure |
| `Timeout` | 4 | Exceeded watchdog or scheduling timeslice |
| `QuotaExceeded` | 5 | Storage or memory quota breached |
| `SecurityViolation` | 6 | Attempted capability breach or forged identity |
| `UnhandledException` | 7 | Unhandled trap or signal |

Crash records are stored in a bounded in-memory ring buffer (64 entries) and exposed via `DiagnosticsService`.
