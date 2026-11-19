# WinIOS Campaign 230 — Stage 230.2–230.5 Package Format v3 & Manifest Specification

## 1. Application Manifest Specification (`format_version=3.0`)
The application manifest defines application metadata, compatibility constraints, and resource limits:
- `bundle_id`: Unique reverse-DNS identifier (max 128 characters, strictly checked against traversal).
- `name` / `display_name`: Human-readable application title.
- `version`: Semantic version string `major.minor.patch`.
- `architecture`: Machine target (`arm64` / `aarch64`).
- `min_sdk_version_u32`: Minimum WinIOS SDK version required (e.g. `0x00020300` for SDK v2.3, `0x00020400` for SDK v2.4).
- `target_abi_version`: Target Category C ABI level (e.g. `2.4`).
- `executable_name`: Relative path to main guest executable inside the package (e.g. `bin/demo`).
- `storage_quota_bytes`: Bounded disk quota for application state.
- `resource_limits`:
  - `max_memory_bytes`: Peak RAM allocation ceiling.
  - `max_instruction_budget`: Per-quantum or total instruction execution budget.
  - `max_open_handles`: Bounded kernel descriptor limit.
- `supported_doc_types`: Array of supported MIME types or extensions for document association (e.g. `text/plain`, `.txt`, `.md`).
- `entry_symbol`: Target entry symbol (e.g. `_main` or `_start`).
- `entry_stack_size`: Allocated user stack space in bytes.
- `category` / `author` / `description`: Metadata fields.
- `permissions` / `optional_permissions`: Requested platform capabilities.

## 2. Package Format v3 Binary Architecture (`WIOSPKG3`)
The package format is an aligned container format with content hashing:
```
+-------------------------------------------------------------+
| Header (48 bytes)                                           |
| - Magic: "WIOSPKG3" (8 bytes)                               |
| - Header Version: 3 (4 bytes)                               |
| - Flags: bit 0 = signed, bit 1 = developer (4 bytes)         |
| - Total Package Size: uint64 (8 bytes)                      |
| - Manifest Offset: uint64 (8 bytes)                         |
| - Manifest Length: uint32 (4 bytes)                         |
| - Entry Count: uint32 (4 bytes)                             |
| - Reserved: uint64 (8 bytes)                                |
+-------------------------------------------------------------+
| Manifest String Data (UTF-8, key=value lines)               |
+-------------------------------------------------------------+
| Directory Table:                                            |
| For each entry:                                             |
| - Path Length: uint16                                       |
| - Path String: char[path_len]                               |
| - Entry Type: uint32 (1=Executable, 2=Resource, 0=File)     |
| - Flags: uint32                                             |
| - Data Offset: uint64 (aligned to 16 bytes)                 |
| - Data Size: uint64                                         |
| - SHA-256 Digest: uint8_t[32]                               |
+-------------------------------------------------------------+
| Payload Blocks (16-byte aligned raw file byte data)         |
+-------------------------------------------------------------+
```

## 3. Package Parser & Validator Boundary Hardening
`AppPackage::validate_package_data()` enforces:
1. **Magic & Header Integrity**: Validates magic string and checks header version (`3`).
2. **Payload Bounds**: Asserts total package size matches buffer size.
3. **Range Collision Checks**: Non-overlapping interval tracking across header, manifest, and all file payloads. Any overlapping entry returns `PackageValidationResult::OverlappingEntries`.
4. **Path Traversal Protection**: Disallows `..`, leading `/` or `\`, drive letters `:`, and null bytes.
5. **Digest Verification**: Each entry's SHA-256 hash is computed and verified against the entry table; tampering returns `PackageValidationResult::ChecksumMismatch`.
6. **Executable Presence**: Verifies the declared `executable_name` exists and is non-empty.
7. **Full Backward Compatibility**: Seamlessly unpacks legacy `WIOSPKG1` and `WIOSPKG2` packages.

## 4. Package Integrity & Trust Policy
`AppPackage::evaluate_trust()` establishes explicit separation of trust levels:
- `PackageTrustLevel::Unsigned`: Package is structurally valid, but lacks publisher verification.
- `PackageTrustLevel::DeveloperLocal`: Development package enabled under local trust policy.
- `PackageTrustLevel::PlatformSigned`: Verified by simulated platform trust root signature.
- **Security Rule**: Capabilities are strictly evaluated at the supervisor/kernel boundary; a package manifest requesting elevated privileges is NOT automatically granted them.
