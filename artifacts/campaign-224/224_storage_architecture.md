# WinIOS Campaign 224 — Stage 224.11 Sandboxed Storage Architecture

## 1. Sandbox Layout & Directory Isolation
Each installed application in WinIOS is granted an isolated private root directory under the VFS:

```
/data/
  └── <bundle_id>/
        ├── config/
        ├── cache/
        └── documents/
```

### Path Sanitization Rules
- Absolute paths are strictly disallowed (`is_path_safe` rejects leading `/` or `\`).
- Colon characters (`:`) are rejected to prevent alternate data stream attacks or drive-letter parsing.
- Traversal tokens (`..`, `/..`, `../`, `..\`) are strictly rejected.
- Null byte injection (`\0`) is detected and rejected.

---

## 2. Recursive Quota Enforcement
Unlike earlier revisions which only audited top-level files in `/data/<bundle_id>/`, Stage 224.11 implements recursive directory traversal in `calculate_dir_usage()`.
Every nested file in subdirectories is counted toward the application's declared storage quota limit ($10\,\text{MB}$ default).
Furthermore, when replacing an existing file via `atomic_write`, the existing file's size is netted out:
$$\text{projected\_usage} = \text{current\_usage} - \text{existing\_size} + \text{new\_size}$$
preventing false `QuotaExceeded` rejections on in-place file rewrites.
