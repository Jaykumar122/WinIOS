# Stage 231.18: Application Generator and SDK Template Validation

## Overview
Stage 231.18 audited and verified the application generator (`AppGenerator`), SDK template source generation, manifest configuration, build script generation, package synthesis, Mach-O structural inspection, publisher signing, transactional installation, and end-to-end launch pipeline.

## Verification Areas

1. **Source Code & Manifest Generation**:
   - Verified generated C source code `#include "winios/winios.h"`, `winios_log_info`, and clean exit `winios_exit(0)`.
   - Verified SDK version targeting `0x00020400` (SDK v2.4).
   - Verified INI manifest serialization and deserialization, including `format_version=3.0`, `min_abi_version=2.4`, `target_abi_version=2.4`, and permission bindings.
   - Verified build script generation targeting `aarch64-freestanding` and Mach-O linking with 16 KB page alignment.

2. **Mach-O Inspection**:
   - Integrated `AppInspector` validating embedded ARM64 binaries (`MH_MAGIC_64`, CPU type `CPU_TYPE_ARM64`, 16 KB granule alignment).
   - Validated that `AppPackage::validate_package_data` passes on the synthesized package container.

3. **End-to-End Signing, Install, and Launch Pipeline**:
   - Registered platform signing key in `PublisherTrustStore`.
   - Signed synthesized package with HMAC-SHA256 authenticated trailer (`WIOSAUTH`).
   - Verified package signature status evaluates to `SignatureStatus::PlatformSigned`.
   - Transactionally installed via `PackageInstaller`, verifying VFS directory layout and `ApplicationRegistry` records.
   - Launched into active process session with `AppLaunchPipeline`, verifying process identity registration and generation allocation.
   - Terminated cleanly with `AppLaunchPipeline::terminate` and confirmed slot unbinding.
   - Uninstalled with `--purge` and verified complete cleanup.

## Test Results
- **Test Target**: `stage231_18_generator_validation_tests` (CTest #920)
- **RelWithDebInfo**: Passed (0.07s)
- **Debug**: Passed (0.09s)
