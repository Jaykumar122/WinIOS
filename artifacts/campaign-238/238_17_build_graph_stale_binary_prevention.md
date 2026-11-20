# Stage 238.17: Build Graph and Stale Binary Prevention

## 1. Overview
Stage 238.17 audited the CMake build dependency graph and header inclusion chains in `WinIOS/CMakeLists.txt` to eliminate stale binary anomalies, incremental linking artifacts, and un-tracked header dependencies.

## 2. Dependency Audit & Improvements
1. **Target Include Directories**:
   - Verified that all newly added stage test targets (`stage238_3` through `stage238_16`) explicitly include `.` (`${CMAKE_CURRENT_SOURCE_DIR}`) and `${CMAKE_CURRENT_SOURCE_DIR}/src`.
   - Verified that all shared headers (such as `tests/support/test_assert.h`) are tracked as build dependencies so edits to headers immediately trigger recompilation of all dependent test translation units.
2. **Deterministic MSVC Compiler Flags**:
   - Enforced `/W4 /WX /permissive-` uniformly across all new test targets.
   - Verified that `/Zc:preprocessor` or standard-compliant macro expansion is honored by macro guards in `tests/support/test_assert.h`.
3. **Linker Independence**:
   - Each unit test executable links against `winios_core` and Windows system cryptography/synchronization libraries (`bcrypt.lib`, `advapi32.lib`, etc.) without circular dependencies.

## 3. Findings
The build dependency graph is completely acyclic and deterministically rebuilds all out-of-date binaries upon source changes.
