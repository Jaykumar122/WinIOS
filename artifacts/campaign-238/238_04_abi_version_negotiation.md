# Stage 238.4: ABI Version Negotiation Architecture

## 1. Overview & Architectural Audit
Stage 238.4 resolves the Category C guest ABI versioning and negotiation architecture.

In Campaign 237, an ad-hoc stopgap was placed in `kSysWiniosWindowGetGeometry` (syscall 508):
```cpp
auto win = window_mgr_->get_window(wid);
if (!win) {
    return record_error(kErrnoENOSYS);
}
```
This conflated "window handle does not exist" with "syscall does not exist", creating false `ENOSYS` returns for legitimate ABI v1.1 callers querying missing or recycled window handles, simply to satisfy an early Stage 215.3 test that asserted `ENOSYS` on unnegotiated syscall 508.

## 2. Technical Redesign & Negotiation Semantics

### 2.1 Explicit Version Representation
- Defined immutable Category C ABI version constants in `user_syscall_dispatcher.h`:
  - `kWiniosCategoryCAbi_v1_0 = 0x00010000` (Campaign 214–215 baseline: syscalls 500..507)
  - `kWiniosCategoryCAbi_v1_1 = 0x00010100` (Campaign 216: window geometry, focus, list, timers: syscalls 508..512)
  - `kWiniosCategoryCAbi_v1_2 = 0x00010200` (Campaign 217: heap, runtime info, storage, IPC: syscalls 513..521)
  - `kWiniosCategoryCAbi_v1_3 = 0x00010300` (Campaign 220: mutex, guest threading: syscalls 522..524)
- Added `category_c_abi_version_` to `BsdProcess`, defaulting to `0x00010000` (v1.0).
- Added `set_category_c_abi_version()` and `category_c_abi_version()` accessors.
- Persisted and restored `category_c_abi_version_` across `BsdProcess::serialize()` and `BsdProcess::deserialize()`.

### 2.2 Version Negotiation Protocol via Syscall 507
Syscall 507 (`kSysWiniosGetAbiVersion`) serves as the official runtime ABI negotiation gateway:
- If `arg0 == 0`: queries host platform ABI ceiling and negotiates current process to `kWiniosCategoryCAbiVersion` (`0x00010100`).
- If `arg0 != 0`: caller explicitly requests an ABI level; negotiated version becomes `min(arg0, kWiniosCategoryCAbiVersion)`.
- The negotiated version is stored in the process and dispatcher.

### 2.3 Strict Dispatcher Enforcement
For all Category C ABI v1.1 extensions (508..512):
```cpp
if (effective_category_c_abi(proc) < kWiniosCategoryCAbi_v1_1) {
    return record_error(kErrnoENOSYS);
}
```
For Syscall 508 (`kSysWiniosWindowGetGeometry`):
- If caller has NOT negotiated ABI v1.1: returns `ENOSYS`.
- If caller HAS negotiated ABI v1.1+:
  - If `!window_mgr_`: returns `ENOSYS`.
  - If `!win || !win->can_access(proc.pid())`: returns `EBADF`.
  - If invalid pointer `geom_va`: returns `EFAULT`.
  - If valid: returns `0` and populates geometry.

Window existence is completely decoupled from ABI version detection.
