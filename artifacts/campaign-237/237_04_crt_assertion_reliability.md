# Stage 237.4 — CRT Assertion Dialog Investigation

## 1. Problem Statement & Historical Context

During automated test suite execution in MSVC Debug configuration (`/MDd` or `/MTd`), standard library assertions (`<cassert>` / `assert()`) invoke the Microsoft C Runtime function `_assert()`. By default on Windows desktop environments, `_assert()` invokes `__crtMessageBoxW`, which displays a modal dialog:
```
---------------------------
Microsoft Visual C++ Runtime Library
---------------------------
Debug Assertion Failed!

Program: ...\stage215_3_abi_hardening_tests.exe
File: ...\stage215_3_abi_hardening_tests.cpp
Line: 52

Expression: WINIOS_CATEGORY_C_ABI_VERSION == 0x00010000

For information on how your program can cause an assertion
failure, see the Visual C++ documentation on asserts.

(Press Retry to debug the application)
---------------------------
Abort   Retry   Ignore   
---------------------------
```
In headless continuous integration and CTest execution environments, this modal message box stalls the test process indefinitely awaiting user input, resulting in test timeouts (e.g., 1500-second default CTest timeout).

---

## 2. Affected Test Targets in Debug Configuration

The following 4 test targets were identified as hanging or aborting with modal CRT dialogs:
1. **#713 (`stage215_3_abi_hardening_tests`)**: Stalled on `assert(WINIOS_CATEGORY_C_ABI_VERSION == 0x00010000)`.
2. **#722 (`stage215_22_security_fuzzing_tests`)**: Stalled on CRT assertion during invalid parameter fuzzing.
3. **#805 (`stage222_15_timer_scheduler_tests`)**: Stalled on timer quantum assertion.
4. **#855 (`stage226_7_keyboard_focus_tests`)**: Stalled on focus hierarchy validation assertion.

---

## 3. Non-Interactive Architecture Without Suppressing Assertions

Suppressing assertions (e.g. by defining `NDEBUG` in Debug mode) is unacceptable as it destroys test rigor. Instead, the correct engineering solution is to redirect the CRT error reporting channels to `stderr` and configure non-interactive termination:

```cpp
#if defined(_WIN32)
#include <crtdbg.h>
#include <stdlib.h>

static void configure_noninteractive_crt() {
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
}
#endif
```

### Verification Properties:
1. **No Modal Dialogs:** No window handle or UI thread is created on assertion failure.
2. **Full Diagnostics Retained:** The failing expression, source file name, and line number are emitted to `stderr` and captured in CTest logs.
3. **Immediate Deterministic Failure:** The process terminates immediately with exit code 3 (`abort()`), completing in <0.05 seconds instead of timing out at 1500 seconds.
4. **Zero Assertion Suppression:** Any unexpected condition continues to immediately fail the test.
