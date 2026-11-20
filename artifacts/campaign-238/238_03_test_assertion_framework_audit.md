# Stage 238.3: Test Assertion Framework Audit

## Overview
Stage 238.3 audits all assertion mechanisms across the test suite, evaluates the replacements of CRT `assert()` with `TEST_ASSERT()`, and introduces a unified, robust assertion header `tests/support/test_assert.h` accompanied by dedicated verification unit tests.

## Audit Findings & Verification Criteria

1. **Single Evaluation Invariance**:
   - Condition expressions are evaluated within `if (!(cond))` inside a standard `do { ... } while (0)` block.
   - Verified that expressions containing side effects (e.g. `++counter`) execute strictly once.

2. **Reliable Failure Detection**:
   - In production test execution, failing assertions output directly to `std::cerr` with `file:line`, the failed expression `#cond`, and any optional diagnostic message.
   - Execution terminates deterministically via `std::exit(1)`, guaranteeing the test runner marks the test failed and preventing unsafe execution into memory corruption or dangling pointer accesses.

3. **Multi-Configuration Retention**:
   - `TEST_ASSERT` definitions are never elided or compiled away under `NDEBUG` or Release configurations (`RelWithDebInfo`), ensuring regression coverage remains active across all builds.

4. **Production vs Test Separation**:
   - Production kernel and subsystem code (`src/`) relies on explicit runtime boundary checks, `record_error()` errno propagation, and status codes rather than test assertion macros.

5. **Extensibility & Overridable Handler**:
   - `test_assert.h` provides an overridable `AssertionFailureHandler` hook (`winios::test::set_assertion_failure_handler`), enabling meta-testing of assertion failure semantics without killing the test runner.
   - Verified via `tests/unit/stage238_3_assertion_framework_tests.cpp`.
