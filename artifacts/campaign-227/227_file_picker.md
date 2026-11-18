# WinIOS Campaign 227: Virtual File Picker Service Architecture

## 1. Design & Security Boundary

The WinIOS Virtual File Picker provides a platform-mediated, out-of-process dialogue mechanism allowing guest applications to request user document selection without granting the calling application arbitrary directory browsing privileges.

In traditional desktop systems, exposing file picker APIs often leaks entire directory hierarchies to client processes. WinIOS isolates the file picker service within the platform supervisor (`winios_core` / desktop compositor):
- The guest application issues an asynchronous picker request (`request_open` or `request_save`).
- The picker dialogue is owned and rendered directly by the desktop compositor.
- The caller process receives only an opaque request identifier (`request_id`).
- When the user selects or confirms a file, the platform registers the target document in the `DocumentRegistry`, issues a scoped capability grant to the caller, and posts a completion event to the caller's event queue.

---

## 2. API Contract & Workflow

### 2.1 Syscall Dispatch

Two dedicated Category C syscalls govern file picker interaction:
- `SYS_WINIOS_DOC_OPEN_PICKER (571)`: Requests an Open Document modal dialogue.
- `SYS_WINIOS_DOC_SAVE_PICKER (572)`: Requests a Save Document modal dialogue with default filename.

### 2.2 Sequence Diagram

```
Guest App (EL0)               Kernel Dispatcher               File Picker Service          Compositor UI
      |                              |                                |                         |
      |-- SYS_DOC_OPEN_PICKER ------>|                                |                         |
      |   (win_h, filter_ext)        |-- request_open() ------------->|                         |
      |                              |                                |-- Render Modal Dialog ->|
      |<-- out_req_id = 1001 --------|<-- return success -------------|                         |
      |                              |                                |                         |
      | (App continues event loop)   |                                |    [User Selects File]  |
      |                              |                                |<-- accept_selection() --|
      |                              |                                |-- Register in DocReg --|
      |                              |                                |-- Grant Access --------|
      |                              |<-- Push Event (PickerDone) ----|                         |
      |<-- Desktop Event (581) ------|                                                          |
```

---

## 3. Sandboxing & Candidate Filtering

The File Picker enforces strict candidate enumeration constraints:
1. **Permitted Roots:** File browsing is strictly restricted to `/Documents`, `/Shared`, `/Downloads`, and `/tmp`.
2. **Extension Filtering:** If the application provides a filter (e.g., `.txt`, `.json`), only matching files are presented as selectable candidates.
3. **Hidden / System Node Denial:** Files in `/System`, `/Library`, `/dev`, or dot-prefixed metadata nodes are excluded from picker candidate lists.
4. **Modal Window Association:** The picker is tied to the caller's window handle. If the caller window is minimized, hidden, or destroyed before completion, the picker request is automatically cancelled and resources cleaned up.
