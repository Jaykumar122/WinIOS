# WinIOS Campaign 223 — Clipboard Interoperability & Security (Stage 223.14)

## 1. Overview

Stage 223.14 audits the system clipboard subsystem (`SystemClipboard`), ensuring capability-gated text sharing and security auditing between guest processes.

---

## 2. Security Capabilities & Audit Log

- **Capabilities**:
  - `Capability::ClipboardRead`: Required to retrieve clipboard text via `get_text`.
  - `Capability::ClipboardWrite`: Required to set or clear clipboard text via `set_text` / `clear`.
- **Payload Clamping**: Maximum clipboard payload size is strictly clamped to `kMaxClipboardBytes = 64 KB` (65,536 bytes). Excess payloads are rejected immediately.
- **Audit Logging**: Every clipboard access (read, write, clear) records:
  - Timestamp (microseconds)
  - Caller PID
  - Action string
  - Data size
  - Security verdict (`allowed == true/false`)

---

## 3. Verification

`tests/unit/stage223_14_clipboard_interop_tests.cpp` (100% PASS):
- Multi-process data transfer between sender (write) and receiver (read).
- Unauthorized process blocking and audit log recording.
- Oversized payload rejection.
- Clipboard clearing and state reset.
