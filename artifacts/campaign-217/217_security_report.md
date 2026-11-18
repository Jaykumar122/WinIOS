# WINIOS CAMPAIGN 217 — SECURITY & BOUNDARY DEFENSE REPORT

**Date:** November 18, 2026  
**Campaign:** 217  
**Host:** Windows 11 x64  
**Scope:** Guest Application Runtime, Isolated Storage, IPC Router, Resource Manager & Allocator Quotas

---

## 1. Executive Summary

Campaign 217 introduced developer-facing runtime services and guest SDK APIs. To prevent guest applications from escalating privileges, escaping sandboxes, or exhausting host resources, strict boundary defenses and security mitigations were designed and validated via adversarial unit tests (`stage217_23_developer_e2e_tests::test_adversarial_security_fuzzing`).

---

## 2. Threat Models & Validated Mitigations

### 2.1 Storage Sandbox Traversal & Quota Exhaustion
- **Threat:** Malicious guest app uses `../` sequences or absolute Windows paths (e.g., `C:\Windows\System32`) to escape its isolated container, or attempts to write large files exhausting host disk space.
- **Mitigation:**
  - `GuestStorageApi` wraps `IsolatedStorage` which enforces strict per-process sandboxing rooted under `vfs/app_data/<bundle_id>/`.
  - Path canonicalization rejects all paths containing `..` or absolute drive roots.
  - Quota enforcement (`quota_bytes_`) rejects writes exceeding the per-process quota.
- **Verification:** Unit tests confirmed attempted traversal with `../../../boot.ini` and oversized writes return `false`/error immediately.

### 2.2 IPC Cross-Process Spoofing & Port Flooding
- **Threat:** Guest process attempts to impersonate another PID, send messages to unowned ports, or flood the router with millions of messages causing Denial of Service.
- **Mitigation:**
  - `GuestIpcRouter` enforces that sender PID is verified by the kernel runtime and stamped directly on the message.
  - Port ownership is strictly enforced: only the owner process can read or close a port.
  - Per-port mailbox queue depth is capped at 64 messages. Once full, `Send` returns `false` (`kQueueFull`).
  - Total ports per process are strictly bounded.
  - `CleanupProcess(pid)` automatically frees all ports and flushes messages on process termination.
- **Verification:** Test cases verified unauthorized `Receive`, queue overflow (>64 messages), and auto-cleanup.

### 2.3 Heap Exhaustion & Use-After-Free
- **Threat:** Unchecked memory allocations crash the host or leak memory across process lifecycles.
- **Mitigation:**
  - `GuestAllocator` bounds heap size and tracks `quota_bytes_`.
  - In-place and bump reallocations strictly verify that net growth does not violate quota or heap bounds.
  - `IsValidPointer` and `GetAllocationSize` validate addresses before servicing memory requests.
  - Double frees are caught and rejected (`Free` returns `false`).
  - Full leak detection via `DetectLeaks` and mandatory zero-leak enforcement on process teardown.
- **Verification:** Test confirmed double-free rejection, quota rejection at 1000 bytes, and leak diagnostic tracking.

### 2.4 Resource Bundle Deserialization & Corrupt Assets
- **Threat:** Crafted malicious `.wiosres` archive with corrupted header, invalid offset, or out-of-bounds BMP dimensions triggering buffer overflows in the host compositor/decoder.
- **Mitigation:**
  - Magic signature `WRES` and format version checked.
  - CRC32 checksums verified over entry contents before unpacking.
  - BMP decoder rejects files larger than 16 MB, dimensions exceeding 4096x4096, non-standard compression (only BI_RGB uncompressed supported), and truncated pixel buffers.
- **Verification:** Corrupted BMP files and invalid bundles fail gracefully without crashing or corrupting memory.

### 2.5 Input Provenance Spoofing
- **Threat:** Synthetic guest event injected into the event loop masquerading as a physical user keystroke or mouse click.
- **Mitigation:**
  - `GuestEvent` carries immutable `EventSourceProvenance` tags (`kPhysicalWindows`, `kSyntheticGuest`, `kSystemEvent`).
  - `GuestEventLoop` preserves input source tags without modification.
- **Verification:** Unit tests assert physical provenance is strictly preserved across the event dispatch pipeline.
