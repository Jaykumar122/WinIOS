# WinIOS Application Resource Bundles Report

**Campaign:** 221 (Stage 221.15)  
**Subsystem:** Package Resources & Asset Resolution  

---

## 1. Resource Bundle Model

Applications bundle localized strings, configuration files, UI layout templates, and bitmaps:
- **Location:** Packed in the `.wios` container under the `res/` directory hierarchy (e.g. `res/icon.bmp`, `res/theme.cfg`).
- **Installed VFS Path:** Placed in `/Applications/<bundle_id>.app/res/` and mirrored to `/resources/<bundle_id>/`.
- **Integrity:** Every resource entry is indexed with a SHA-256 digest in the package manifest and validated upon installation.
- **Access Protocol:** Guest applications open bundled resources via `WINIOS_SYS_RESOURCE_OPEN` (syscall 532) or standard VFS path reading within their sandboxed bundle.
