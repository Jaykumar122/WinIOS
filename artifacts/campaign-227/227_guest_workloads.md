# WinIOS Campaign 227: Native ARM64 Guest Workloads & Verification

## 1. Overview & Workload Objectives

Campaign 227 delivers two native ARM64 guest applications compiled as genuine freestanding Mach-O binaries for the iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`) target profile:

1. **WinIOS Text Editor (`campaign227_text_editor.c`):**
   - Package / Bundle: `com.winios.texteditor`
   - Role: Document authoring, stream creation, atomic save, and access delegation.
   - Target PID: 95.
   - Clean Exit Code: 95.

2. **WinIOS Document Viewer (`campaign227_doc_viewer.c`):**
   - Package / Bundle: `com.winios.docviewer`
   - Role: Read-only inspection, cross-process grant consumption, metadata verification.
   - Target PID: 96.
   - Clean Exit Code: 96.

---

## 2. Compilation & Toolchain Pipeline

Cross-compilation is executed on Windows 11 host using Zig 0.17.0 targeting Darwin ARM64:

```powershell
zig cc -target aarch64-macos -nostdlib -fno-builtin -fno-sanitize=all -O2 `
    -I guest-sdk/include `
    src/apps/campaign227_text_editor.c guest-sdk/lib/winios_runtime.c `
    -o build/campaign227_text_editor.macho

zig cc -target aarch64-macos -nostdlib -fno-builtin -fno-sanitize=all -O2 `
    -I guest-sdk/include `
    src/apps/campaign227_doc_viewer.c guest-sdk/lib/winios_runtime.c `
    -o build/campaign227_doc_viewer.macho
```

### 2.1 Binary Characteristics
- **Mach-O Type:** `MH_EXECUTE` (Mach-O 64-bit executable ARM64).
- **Segments:** `__PAGEZERO` (4GB guard), `__TEXT` (read-only executable), `__DATA` (read-write data).
- **Runtime Library:** Links `guest-sdk/lib/winios_runtime.c`, supplying `_start` CRT bootstrap, software 8x8 font rendering, drawing primitives, and typed Category C ABI v2.1 wrappers.
- **Binary Sizes:** Text Editor = 51,520 bytes; Document Viewer = 51,632 bytes.

---

## 3. End-to-End Execution Trace

### 3.1 Text Editor Execution (PID 95)
1. Creates desktop window (480x320 at 60, 60) via `SYS_WINIOS_WINDOW_CREATE_V2`.
2. Opens document stream for `notes.txt` (handle `0x100000001`) via `SYS_WINIOS_DOC_STREAM_OPEN`.
3. Writes string: `"Campaign 227 Document Editor Active\nText Buffer Verified.\n"` via `SYS_WINIOS_DOC_STREAM_WRITE`.
4. Closes stream via `SYS_WINIOS_DOC_STREAM_CLOSE`, triggering atomic save to `/Documents/notes.txt`.
5. Grants read access to Document Viewer (PID 96) via `SYS_WINIOS_DOC_GRANT_ACCESS`.
6. Sets desktop clipboard banner: `"Campaign 227 Text Editor Active"`.
7. Destroys window and terminates with exit code 95 via `SYS_exit`.

### 3.2 Document Viewer Execution (PID 96)
1. Creates viewer window (400x300 at 560, 60) via `SYS_WINIOS_WINDOW_CREATE_V2`.
2. Queries metadata for handle `0x100000001` via `SYS_WINIOS_DOC_GET_METADATA`.
3. Opens document stream in `Read` mode via `SYS_WINIOS_DOC_STREAM_OPEN` using granted permissions.
4. Reads back the exact bytes committed by the editor via `SYS_WINIOS_DOC_STREAM_READ`.
5. Reads clipboard text to confirm synchronized desktop context.
6. Destroys window and terminates cleanly with exit code 96.
