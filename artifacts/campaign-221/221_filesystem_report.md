# WinIOS Guest Filesystem Foundation Report

**Campaign:** 221 (Stage 221.8)  
**Subsystem:** Virtual File System (VFS) & Namespace Isolation  

---

## 1. Virtual Namespaces

WinIOS establishes virtual path namespaces for guest execution:
- `/system`: System libraries, configurations, and core runtime definitions (read-only for normal applications).
- `/apps`: Installed application bundles (`/Applications/<bundle_id>.app/`).
- `/data`: Sandboxed per-application persistent storage (`/data/<bundle_id>/`).
- `/tmp`: Ephemeral per-session temporary files.
- `/resources`: Bundled read-only assets and metadata fixtures.

---

## 2. Path Normalization & Security Boundary

1. **Path Normalization:** Relative paths (`foo/bar`) are resolved against the active working directory or storage root.
2. **Directory Traversal Defense:** Any path containing `..` or attempted escape beyond the assigned root is intercepted and denied with `kErrnoEPERM` or `kErrnoEACCES`.
3. **File Descriptor Table:** Each `BsdProcess` owns an independent `FileDescriptorTable` (`kMaxFiles = 64`), preventing cross-process descriptor hijacking.
