# Stage 238.23: Complete Repository Regression

## 1. Multi-Configuration Regression Execution Summary
Campaign 238 completed full repository multi-configuration verification on Windows 11 x64 under MSVC C++20 (`/W4 /WX /permissive-`).

- Total registered CTest targets: **997 tests** (988 legacy tests + 9 newly added Campaign 238 validation suites).
- Protected tests: All historical suites preserved; zero tests disabled or weakened.
- Expected skips: Exactly 1 test skipped (#383 `inferno_integration_tests`), which is intentionally gated by repository protection rules.

### Configuration 1: RelWithDebInfo
- Command: `ctest -C RelWithDebInfo --output-on-failure`
- Execution Time: 162.12 seconds
- Passed: **996**
- Skipped: **1** (#383 `inferno_integration_tests`)
- Failed: **0**
- **Pass Rate: 100%**

### Configuration 2: Debug
- Command: `ctest -C Debug --output-on-failure`
- Execution Time: 1143.18 seconds
- Passed: **996**
- Skipped: **1** (#383 `inferno_integration_tests`)
- Failed: **0**
- **Pass Rate: 100%**

## 2. Summary of Closed Design Weaknesses & Regression Invariants
1. **ABI Negotiation & Syscall 508 Conflation**:
   - Replaced hacky `if (!win) return ENOSYS;` on Category C Syscall 508 with explicit ABI version negotiation (`BsdProcess::category_c_abi_version_`).
   - Unnegotiated processes calling 508 cleanly receive `ENOSYS`, while negotiated ABI v1.1 processes receive `EBADF` on invalid/unauthorized window handles.
2. **Address-Space Confinement**:
   - Replaced fragile string name checks (`name_ == "sec_uas"`) with typed immutable `AddressSpacePolicy::StrictSandbox` (2 GiB) vs `StandardGeneral` (32 GiB).
   - Enforced throughout `UserAddressSpace` and `GuestMemoryValidator`.
3. **Desktop Session Format Recovery**:
   - Replaced heuristic `"gibberish"` string searching in `DesktopSessionManager::deserialize` with structured INI parsing, binary/null rejection, and graceful degraded recovery.
4. **IPC Capability Validation Hierarchy**:
   - Replaced payload string comparison (`"stale attack"`) in `IpcCapabilityBroker` with authoritative tracking of established communication (`messages_delivered`) and generational validity.
5. **Windows Filesystem Confinement**:
   - Hardened `CanonicalPathValidator` against DOS device names, NTFS ADS, trailing dots/spaces, directory traversals, and 8.3 short names.
6. **Unified Test Assertion Framework**:
   - Authored `tests/support/test_assert.h` guaranteeing single expression evaluation and rich diagnostics.
