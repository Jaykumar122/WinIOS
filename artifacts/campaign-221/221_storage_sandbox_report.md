# WinIOS Persistent Per-Application Storage Report

**Campaign:** 221 (Stage 221.9)  
**Subsystem:** `IsolatedStorage` & Cross-Application Isolation  

---

## 1. Storage Isolation Architecture

Each installed application is allocated a dedicated storage root at `/data/<bundle_id>/`:
- **Strict Partitioning:** Application A (`com.winios.calc`) is restricted entirely to `/data/com.winios.calc/`. Any request to access files in `/data/com.winios.notes/` without explicit `StorageShared` permission is rejected with `AccessDenied` (`kErrnoEPERM`).
- **Quota Enforcement:** Default storage quota is set to 10 MB per application (`kDefaultAppQuotaBytes = 10 * 1024 * 1024`). Attempts to write data exceeding the remaining quota fail with `QuotaExceeded` (`kErrnoENOSPC`).
- **Atomic File Replacement:** `atomic_write()` writes payload to a temporary file (`.tmp`), computes SHA-256 integrity, flushes data to disk, and executes an atomic rename to prevent partial writes during crashes.
- **Data Retention Across Restarts:** Storage backing is persisted to host-side directory paths, enabling applications like Notes to reload state across runtime restarts.
