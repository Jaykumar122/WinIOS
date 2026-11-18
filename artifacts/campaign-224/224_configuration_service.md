# WinIOS Campaign 224 — Stage 224.17 Application Configuration Service

## 1. Typed Configuration Storage
The `ConfigurationService` (`src/services/configuration_service.h`) provides isolated, persistent key-value configuration storage for installed guest applications:

### Supported Types
- `String`: UTF-8 strings up to 1024 bytes (`kMaxValueLength`).
- `Int64`: Signed 64-bit integer values.
- `Double`: IEEE 754 double precision floating point.
- `Bool`: Boolean flag (`true` / `false`).

---

## 2. Resource Containment & Isolation
- **Key Constraints:** Keys are restricted to alphanumeric identifiers up to 64 bytes (`kMaxKeyLength`).
- **Quota Bounds:** Each application is strictly limited to 128 active keys (`kMaxEntriesPerApp`).
- **Namespace Isolation:** Configuration namespaces are partitioned by `bundle_id`. Access to an application's configuration requires `Capability::ConfigAccess` and identity verification matching the caller's bound `bundle_id`.
- **Durable Persistence:** Configurations are serialized to and loaded from `/data/<bundle_id>/config.kv` on the sandboxed VFS.
