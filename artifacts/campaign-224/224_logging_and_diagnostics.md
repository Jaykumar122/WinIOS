# WinIOS Campaign 224 — Stage 224.18 Structured Logging and Diagnostics Service

## 1. Structured Leveled Logging
`DiagnosticsService` (`src/services/diagnostics_service.h`) implements centralized, bounded log collection for the guest runtime and applications:

```cpp
enum class LogLevel : std::uint32_t {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Fatal = 4
};

struct LogEntry {
    std::uint64_t sequence;       // Monotonically increasing sequence number
    std::uint64_t timestamp_us;   // Monotonic microseconds
    std::string   bundle_id;      // Emitting application
    std::uint32_t pid;            // Emitting PID
    LogLevel      level;          // Severity level
    std::string   message;        // Log payload
};
```

Log entries are captured into a circular ring buffer with a 512-entry capacity ceiling (`kMaxLogCapacity`), guaranteeing that logging does not grow unbounded or exhaust host memory.

---

## 2. Platform Diagnostics Summary
The `get_summary()` API collects an instantaneous snapshot of platform health:
- Active running processes.
- Total installed applications in `ApplicationRegistry`.
- Open composited windows.
- Active IPC port count.
- Total recorded crash count.
- Security audit metrics (`total_checks`, `total_grants`, `total_denials`).

Querying diagnostics requires `Capability::DiagnosticsAccess`.
