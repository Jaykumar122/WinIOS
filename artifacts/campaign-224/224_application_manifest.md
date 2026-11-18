# WinIOS Campaign 224 — Stage 224.4 Application Manifest Specification

## 1. Schema Definition
The Application Manifest (`PackageManifest`) establishes the declarative metadata contract for an application on the WinIOS research platform.

### Manifest Keys
```ini
format_version=2.1
bundle_id=com.winios.settings
name=Platform Settings
version=1.0.0
min_os_version=224.0
architecture=arm64
min_abi_version=1.7
executable_name=bin/settings
storage_quota_bytes=10485760
signature_sha256=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
icon_path=res/icon.png
startup_config={"theme":"dark","refresh_rate":60}
permissions=display.window,storage.private,config,logging
optional_permissions=clipboard.read,clipboard.write
```

---

## 2. Parsing and Validation Rules
1. `bundle_id`: Non-empty alphanumeric string with reverse-DNS notation (`com.org.app`). Must not contain directory separators or `..`.
2. `name`: Human-readable display string.
3. `version`: Semantic version format `major.minor.patch`.
4. `architecture`: Must be `arm64` or `aarch64`.
5. `executable_name`: Relative path to entry binary within the package.
6. `storage_quota_bytes`: Numeric storage quota limit in bytes (defaults to 10 MB).
7. `permissions`: Comma-separated list of required capabilities requested by the application.
8. `optional_permissions`: Comma-separated list of non-critical capabilities.
9. `startup_config`: Optional default configuration string provided to `ConfigurationService`.
