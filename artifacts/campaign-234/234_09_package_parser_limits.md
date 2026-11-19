# Stage 234.9: Canonical Package and Parser Resource Limits Audit

## Executive Summary
Stage 234.9 rigorously audits and enforces structural, resource, and mathematical limits during package decompression and validation (`AppPackage::validate_package_data` in `src/packaging/app_package.cpp`). The parser was hardened with active enforcement of cumulative uncompressed payload storage quotas, maximum nesting depth, and directory segment formatting, defending against memory exhaustion, recursive traversal, and zip bomb/allocation attacks.

## 1. Audited Resource Limits & Enforcement

| Limit Category | Threshold / Policy | Enforcement Mechanism | Failure Diagnostic |
| :--- | :--- | :--- | :--- |
| **Manifest Size** | Max 65,536 bytes (64 KB) | Checked before buffer read / allocation | `PackageValidationResult::ManifestTooLarge` |
| **Manifest Integer Math** | `offset + length <= size` | `security::checked_add` | `PackageValidationResult::IntegerOverflow` |
| **File Count** | Max 256 entries | Checked against V3 / V2 header counter | `PackageValidationResult::FileCountExceeded` |
| **Individual File Size** | Max 33,554,432 bytes (32 MB) | Checked against container size & cap | `PackageValidationResult::FileCountExceeded` |
| **Cumulative Payload Quota** | `cumulative_payload <= manifest.storage_quota_bytes` | Summed with `checked_add` per entry | `PackageValidationResult::QuotaExceeded` |
| **Directory Nesting Depth** | Max 16 levels (`/`) | Slashes counted; > 16 rejected | `PackageValidationResult::PathTraversalAttempt` |
| **Empty Path Segments** | No `//`, no trailing `/` or `\` | Explicit substring & endpoint checks | `PackageValidationResult::PathTraversalAttempt` |
| **Data Offset Math** | `data_off + data_sz <= size` | `security::checked_add` | `PackageValidationResult::IntegerOverflow` |
| **Payload Allocations** | Bounded by physical input buffer | Allocations strictly sized by `data_sz <= size` | `PackageValidationResult::PayloadTruncated` |

## 2. Decompression Bomb & Exhaustion Defense
1. **Uncompressed Streams:** The WIOSPKG v3 format does not perform in-band zlib/deflate expansion; payloads are uncompressed binary blocks verified via SHA-256 digests.
2. **Pre-Allocation Bounds:** Payload memory (`fdata`) is never allocated until after bounds checks guarantee `data_off + data_sz <= size` and `data_sz <= 32 MB`. Therefore, malicious compressed ratios cannot cause memory exhaustion beyond the input buffer.
3. **Cumulative Storage Enforcement:** Previously, `QuotaExceeded` was present in `PackageValidationResult` but not validated during package parsing. In Stage 234.9, cumulative uncompressed file bytes are tracked using overflow-checked arithmetic (`security::checked_add`) and checked against `manifest.storage_quota_bytes`.

## 3. Test Verification
The dedicated suite `tests/unit/stage234_9_package_parser_limits_tests.cpp` was compiled with MSVC C++20 (`/W4 /WX /permissive-`) and executed:
- `RelWithDebInfo`: Passed 100%
- `Debug`: Passed 100%
- Regressions on `stage231_5_parser_hardening_tests` and `stage233_9_parser_canonicalization_tests`: Passed 100%
