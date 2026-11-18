# WinIOS Application Manager & Registry Report

**Campaign:** 221 (Stage 221.18)  
**Subsystem:** `ApplicationRegistry` & Lifecycle Management  

---

## 1. Installed Application Management

`ApplicationRegistry` maintains persistent records of all admitted applications:
- **Registry Schema:** `bundle_id`, `name`, `version`, `exec_path`, `install_dir`, `permissions`, `install_time_us`, and `is_active`.
- **Query APIs:**
  - `list_apps()`: Enumerate all currently installed applications.
  - `get_app(bundle_id)`: Retrieve specific metadata.
  - `is_installed(bundle_id)`: Quick membership test.
- **De-registration:** `unregister_app(bundle_id)` cleanly purges the application registration upon uninstall.
- **State Integrity:** Thread-safe operations protected by `std::mutex lock_`.
