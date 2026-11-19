# Stage 231.17: Developer CLI Security and Reliability Verification

## Overview
Stage 231.17 audited, hardened, and verified the developer command-line interface `WiniosAppCli` across all application lifecycle and package inspection commands.

## Implemented Security Controls

1. **Standardized Exit Code Semantics (`CliExitCode`)**:
   - `0 (Success)`: Command completed successfully.
   - `1 (InvalidArgs)`: Invalid syntax, missing required parameters, unknown options, path traversal attempts, or invalid identifiers.
   - `2 (ExecutionFailure)`: Runtime failure during execution (package validation failed, launch rejection, process not found or termination failed).
   - `3 (ServiceUnavailable)`: Required platform service manager pointer is null.

2. **VFS Path Canonicalization & Boundary Enforcement**:
   - Implemented `CanonicalPathValidator::validate_and_normalize_vfs_path`.
   - Normalizes path separators, resolves `.` and `..` without escaping root `/`.
   - Rejects Windows reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`).
   - Rejects drive letters (`C:`), alternate data streams (`:`), UNC network paths (`//`), and relative paths.
   - Integrated into `pkg-inspect`, `app-inspect`, `pkg-validate`, and `app-install`.

3. **Identifier & PID Hardening**:
   - Added `is_valid_bundle_id`: enforces reverse-DNS naming, alphanumeric + hyphen/underscore, forbids traversal characters, shell delimiters (`;`, `` ` ``), and leading/trailing/consecutive dots.
   - Added strict `parse_pid`: requires decimal digits only, forbids negative/zero/overflow values, rejecting trailing junk characters.
   - Safe process lookup in `app-terminate` ensuring active generation verification prior to termination.

4. **Command Surface Verification**:
   - `app-list`: verifies `--active-only` flag and rejects unknown arguments.
   - `app-info`: enforces valid bundle ID syntax and standardized not-found return code.
   - `app-status`: verifies bundle ID syntax and reports running/stopped state.
   - `pkg-inspect` / `app-inspect`: validates VFS package path and displays manifest details.
   - `pkg-validate`: checks package integrity and trust level.
   - `app-install`: verifies path, unpacks package, and installs through `PackageInstaller`.
   - `app-uninstall`: validates bundle ID and `--purge` option.
   - `app-launch`: validates bundle ID and executes through transactional `AppLaunchPipeline`.
   - `app-terminate`: validates PID and terminates through `AppLaunchPipeline`.
   - `diag-query`: supports `--limit <count>` (bounded 1-1000) and displays formatted logs.

## Test Results
- **Test Target**: `stage231_17_cli_security_tests` (CTest #919)
- **RelWithDebInfo**: Passed (0.06s)
- **Debug**: Passed (0.09s)
- **Regression Verification**: `stage230_10_app_cli_generator_tests` passed (0.05s)
