# WinIOS Campaign 227: Desktop Session Persistence Architecture

## 1. Overview & Objectives

Desktop Session Persistence captures the complete operational state of the WinIOS desktop environment—including open application windows, layout geometries, stacking order, active document references, and recent file records—and persists it across system restarts.

---

## 2. Session Schema & Storage

Desktop sessions are stored at `/System/Library/Sessions/desktop_session.json` (encoded in a structured, fault-tolerant key-value format):

```ini
session_version=1
save_timestamp_us=1731952000000000
display_width=800
display_height=600
active_window_index=0
window_count=2

[window]
bundle_id=com.winios.texteditor
title=WinIOS Text Editor
x=60
y=60
width=480
height=320
state=0
z_order=1
associated_doc_path=/Documents/notes.txt

[window]
bundle_id=com.winios.docviewer
title=WinIOS Document Viewer
x=560
y=60
width=400
height=300
state=0
z_order=0
associated_doc_path=/Documents/notes.txt

recent_count=1
[recent]
bundle_id=com.winios.texteditor
display_name=notes.txt
canonical_path=/Documents/notes.txt
last_opened_time_us=1731951950000000
```

---

## 3. Restoration & Defensive Sanitization

Restoration is defensive and resilient against state drift:
1. **Uninstalled App Pruning:** Each window's `bundle_id` is validated against `ApplicationRegistry`. If an application is no longer installed, its window is skipped.
2. **Missing Document Pruning:** If `associated_doc_path` points to a file that has been deleted from VFS, the window is restored with a blank state rather than crashing.
3. **Display Clamping:** Window positions `(x, y, width, height)` are clamped to current display boundaries (`800x600`), ensuring off-screen windows are brought back into visible space.
4. **Corrupt File Resilience:** Truncated files, zero-byte files, or malformed records fail gracefully (`return false`) without bringing down the compositor.
