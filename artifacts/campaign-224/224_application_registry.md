# WinIOS Campaign 224 — Stage 224.5 Application Registry

## 1. Registry Architecture
The `ApplicationRegistry` (`src/packaging/app_registry.h`) is the authoritative platform component for recording installed, active, and updated guest applications.

### State Transitions
```
NotInstalled  ──(Install)──>  Registered & Active  <──(Update)──>  Updated
                                    │                                 │
                               (Deactivate)                      (Deactivate)
                                    ▼                                 ▼
                             Registered & Inactive             Registered & Inactive
                                    │
                               (Uninstall)
                                    ▼
                               NotInstalled
```

---

## 2. API Contract
- `register_app(AppRegistration app)`: Registers a newly installed application. Fails if `bundle_id` already exists.
- `unregister_app(const std::string& bundle_id)`: Removes application entry from registry.
- `update_app(AppRegistration app)`: Updates metadata (version, permissions, timestamps, config).
- `set_active(const std::string& bundle_id, bool active)`: Toggles active status without removing application files.
- `get_app(const std::string& bundle_id)`: Retrieves immutable copy of registration metadata.
- `list_apps(bool active_only = false)`: Returns snapshot vector of installed applications.
- `serialize() / deserialize()`: Deterministic plain-text persistence into `/etc/apps.reg`.
