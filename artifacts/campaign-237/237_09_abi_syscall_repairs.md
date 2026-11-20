# Campaign 237 — Stage 237.9: ABI & Syscall Compatibility Repairs

## 1. Executive Summary

Stage 237.9 resolved all failures in Cluster 1 (Hardcoded Legacy ABI Version Checks) and Cluster 2 (Legacy 2 GiB Address Space Assumptions):
- `stage211_7_user_address_space_tests` (Test #641)
- `stage214_5_abi_boundary_tests` (Test #694)
- `stage215_3_abi_hardening_tests` (Test #713)
- `stage216_19_abi_v11_tests` (Test #734)
- `stage217_19_sdk_apps_tests` (Test #743)

All 5 test suites now pass 100% in both `RelWithDebInfo` and `Debug` configurations.

---

## 2. Root Cause Analysis & Technical Repairs

### 2.1 Forward-Compatible ABI Versioning (#694, #734, #743)
- **Root Cause**: Tests written in Campaigns 214–217 performed strict equality assertions against initial snapshot constants (`kWiniosCategoryCAbiVersion == 0x00010000`, `WINIOS_CATEGORY_C_ABI_VERSION == 0x00010100`, `WINIOS_SDK_VERSION == 0x00010200`). As subsequent campaigns extended the Category C syscall table (up to v1.9 `0x00010900`), the newer backward-compatible ABI constants caused legacy assertions to fail.
- **Correction**: Updated assertions to verify forward compatibility (`>= 0x00010000`, `>= WINIOS_CATEGORY_C_ABI_VERSION_1_1`, `>= WINIOS_SDK_VERSION_1_2`), ensuring that any backward-compatible newer ABI passes historical tests without breaking architectural invariants.

### 2.2 Category C Syscall Range Boundary (#694, #734)
- **Root Cause**: Early tests asserted that syscalls 508 and 513 were not Category C (`!is_category_c_extension(508)` and `!is_category_c_extension(513)`). Later campaigns legitimately expanded Category C to include window geometry (508..511) and heap allocation (513..514).
- **Correction**: Updated upper-bound check in test suites to test against the dynamically defined ceiling `!is_category_c_extension(kSysWiniosMaxCategoryC + 1)`.

### 2.3 32 GiB ARM64 User Address Space Boundary (#641)
- **Root Cause**: In `tests/unit/stage211_7_user_address_space_tests.cpp`, the test asserted that mapping at `0x80000000ULL` (2 GiB) should fail as "beyond user space limit". In modern WinIOS, `kUserSpaceLimitVa` is 32 GiB (`0x0000000800000000ULL`).
- **Correction**: Updated the boundary test to assert that mapping at or beyond `kUserSpaceLimitVa` is rejected.

---

## 3. Verification Matrix

| Test Suite | Test # | Configuration | Result |
|---|---|---|---|
| `stage211_7_user_address_space_tests` | 641 | RelWithDebInfo / Debug | **PASSED** (100%) |
| `stage214_5_abi_boundary_tests` | 694 | RelWithDebInfo / Debug | **PASSED** (100%) |
| `stage215_3_abi_hardening_tests` | 713 | RelWithDebInfo / Debug | **PASSED** (100%) |
| `stage216_19_abi_v11_tests` | 734 | RelWithDebInfo / Debug | **PASSED** (100%) |
| `stage217_19_sdk_apps_tests` | 743 | RelWithDebInfo / Debug | **PASSED** (100%) |

Zero regressions.
