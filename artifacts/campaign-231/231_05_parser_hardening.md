# Campaign 231 — Stage 231.5: WIOSPKG Parser Hardening

## Objective
Harden the binary package validation and unpack pipeline in `AppPackage::validate_package_data` and `AppPackage::evaluate_trust`:
1. Enforce strict integer overflow protection on all offset and length calculations (manifest offset, manifest length, file entry offsets, file entry sizes) via `winios::security::checked_add`.
2. Reject overlapping file payload ranges and entries claiming space outside the container buffer.
3. Eliminate duplicate and case-collision paths (e.g. `bin/main` vs `BIN/MAIN`) preventing NTFS host collision vulnerabilities while preserving guest case sensitivity.
4. Enforce strict UTF-8 validation and reject null byte (`\0`) and control character (`< 0x20`) injection in file paths and manifest strings.
5. Reject declared payload sizes exceeding physical container buffers.
6. Integrate authentic `PublisherTrustStore` envelope evaluation into `AppPackage::evaluate_trust`, replacing the legacy 64-character hex heuristic.

## Implementation Details
- **Files Modified:**
  - `src/packaging/app_package.h`: Added `PublisherTrustStore` forward declaration and overloaded `evaluate_trust(raw_pkg_data, raw_pkg_size, store, allow_dev)` method signature.
  - `src/packaging/app_package.cpp`: Implemented `is_valid_utf8_path` validation, safe integer math across V1/V2/V3 package parsers, case-insensitive collision tracking (`seen_lower_paths`), embedded null checks, and integrated trust store evaluation.
  - `CMakeLists.txt`: Registered unit test target `stage231_5_parser_hardening_tests`.
  - `tests/unit/stage231_5_parser_hardening_tests.cpp`: Unit tests for parser attack surfaces.

## Verification & Test Results
- Test Target: `stage231_5_parser_hardening_tests` (Test #907)
- Configurations Tested:
  - `RelWithDebInfo`: Passed (0.05 sec)
  - `Debug`: Passed (0.07 sec)
- Test Coverage:
  - `test_integer_overflow_rejection`: Manifest offset overflow and directory entry data offset overflow tested and confirmed rejected with `PackageValidationResult::IntegerOverflow`.
  - `test_duplicate_path_collision_rejection`: Case-insensitive path collision (`bin/main` vs `BIN/MAIN`) rejected with `PackageValidationResult::DuplicateFileEntry`.
  - `test_null_char_and_control_injection`: Embedded null bytes in path rejected with `PathTraversalAttempt`, control codes rejected with `PathTraversalAttempt`, null byte in manifest rejected with `InvalidManifest`.
  - `test_declared_size_and_overlapping_entries`: Declared size exceeding container rejected with `PayloadTruncated`, overlapping payload ranges rejected with `OverlappingEntries`.
  - `test_hardened_trust_evaluation`: Verified raw envelope signature evaluation against `PublisherTrustStore` (PlatformSigned, DeveloperSigned, CorruptSignature) and confirmed legacy `"PLATFORM_ROOT_A16_TRUSTED"` test compatibility.
- Regression: Full suite tests 870..907 passed 100% (38/38) in 1.49 sec.
