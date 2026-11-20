# Stage 239.1: Campaign 238 Architecture Audit

## 1. Overview & Verification of Campaign 238 Subsystems
A comprehensive audit of the Campaign 238 implementations was performed:

1. **Category C ABI Negotiation (Syscall 507/508)**:
   - `BsdProcess::category_c_abi_version_` defaults to `0x00010000` (v1.0).
   - Calling Syscall 507 (`kSysWiniosGetAbiVersion`) negotiates the process to `0x00010100` (v1.1).
   - Syscall 508 (`kSysWiniosWindowGetGeometry`) verifies `effective_category_c_abi(proc) >= kWiniosCategoryCAbi_v1_1`; if unnegotiated, it returns `ENOSYS`. When negotiated, invalid/unauthorized handles return `EBADF`.
   - Result: Working as specified; completely eliminated `if (!win) return ENOSYS;`.
2. **AddressSpacePolicy Object Model**:
   - `AddressSpacePolicy` is an immutable typed enum (`StandardGeneral` vs `StrictSandbox`).
   - `UserAddressSpace` enforces immutable limits (`0x800000000ULL` / 32 GiB vs `0x80000000ULL` / 2 GiB).
   - `GuestMemoryValidator` enforces `uas.user_space_limit()`.
   - Result: Eliminates string-based sandbox determination.
3. **Desktop Session Manager Deserialization**:
   - Replaced substring search for `"gibberish"` with format identification: checks for NUL bytes, JSON/XML start tokens, and unsupported version numbers (`> 2`).
   - Degraded valid INI payloads gracefully recover an empty session layout.
   - Result: Structural format safety verified.
4. **IPC Capability Broker Generational State**:
   - Generational rollover vs auto-revocation semantics are differentiated by tracking `messages_delivered`.
   - Completely excised payload inspection (`"stale attack"`).
   - Result: Clean generational semantics across both legacy and new tests.
5. **Unified Test Assertion Framework**:
   - `tests/support/test_assert.h` provides single evaluation of expression and rich diagnostics.

## 2. Remaining Architectural Debt & Gaps for Campaign 239
1. **Application Lifecycle State Disparity**:
   - `BsdProcess` tracks low-level process states (`Starting`, `Runnable`, `Sleeping`, `Stopped`, `Zombie`).
   - `ApplicationRegistry` tracks installation states (`Installed`, `Upgrading`, `Corrupted`, `Uninstalled`).
   - Higher-level application lifecycle states (admitted, loading, initialized, running, suspended, terminating, failed) are scattered across controllers without a unified state machine or transition validation.
2. **Lack of Explicit SDK/Runtime Version Negotiation Protocol**:
   - ABI negotiation is currently limited to Category C ABI v1.0 -> v1.1.
   - There is no unified handshake between package manifest minimum SDK, runtime build version, guest SDK headers, and ABI capability masks.
3. **No Centralized Syscall Registry**:
   - Syscall dispatch is distributed across a large switch-case in `UserSyscallDispatcher` without a single canonical table documenting arguments, memory rules, return codes, and ABI versions.
4. **Implicit / Dispersed Resource Limits**:
   - Per-application resource bounds (memory quotas, IPC queue sizes, window limits, timers, CPU step budgets) are hardcoded across different modules rather than driven by an explicit `AppResourceBudget` policy.
