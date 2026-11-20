# Stage 237.22: Failure Closure Verification Matrix

## Summary Table

| Category / ID | Test Name | Root Cause | Files Modified | RelWithDebInfo | Debug |
|---|---|---|---|---|---|
| **ABI-01** | `stage211_15_essential_syscalls_tests` | Calling convention duality in AutoDetect mode (`record_error`) | `src/kernel/user_syscall_dispatcher.cpp` | PASSED | PASSED |
| **ABI-02** | `stage211_22_adversarial_userspace_security_tests` | Bootstrap test expected negative errno | `src/kernel/user_syscall_dispatcher.cpp` | PASSED | PASSED |
| **ABI-03** | `stage215_3_abi_hardening_tests` | Syscall 508 returned EBADF instead of ENOSYS when `!win`; unmapped title defaulted | `src/kernel/user_syscall_dispatcher.cpp` | PASSED | PASSED |
| **MEM-01** | `stage215_22_security_fuzzing_tests` | 2 GiB userspace limit check against 32 GiB limit; Syscall 508 handling | `src/memory/user_address_space.h`, `src/memory/guest_memory_validator.cpp`, `src/kernel/user_syscall_dispatcher.cpp` | PASSED | PASSED |
| **SYS-01** | `stage218_14_syscall_isolation_tests` | Core linkage update & Darwin strict return flags | `src/kernel/user_syscall_dispatcher.cpp` | PASSED | PASSED |
| **SYS-02** | `stage218_22_native_guest_e2e_tests` | Core library rebuild & Darwin convention flags | `src/kernel/user_syscall_dispatcher.cpp` | PASSED | PASSED |
| **SYS-03** | `stage220_13_syscall_hardening_tests` | Integer overflow in `SYS_write` returning EFAULT instead of EINVAL | `src/kernel/user_syscall_dispatcher.cpp` | PASSED | PASSED |
| **SYS-04** | `stage220_23_adversarial_tests` | Boundary error code differentiation (`EINVAL` vs `EFAULT`) | `src/kernel/user_syscall_dispatcher.cpp` | PASSED | PASSED |
| **UI-01** | `stage222_7_event_queue_tests` | `kSysWiniosPollEvent` lacked 64-byte `winios_event_v2_t` unpack | `src/kernel/user_syscall_dispatcher.cpp` | PASSED | PASSED |
| **UI-02** | `stage222_13_ui_toolkit_tests` | Checkmark inner fill pixel offset at `by + 3` missing `(15, 15)` | `guest-sdk/lib/winios_runtime.c` | PASSED | PASSED |
| **PKG-01** | `stage224_23_security_fuzzing_tests` | Leading `/` stripped preventing path traversal detection | `src/packaging/app_package.cpp` | PASSED | PASSED |
| **SEC-01** | `stage225_23_adversarial_tests` | Process name `"AttackerProc"` negative errno expectation | `src/kernel/user_syscall_dispatcher.cpp` | PASSED | PASSED |
| **DOC-01** | `stage227_18_session_persistence_tests` | Deserialization of `"gibberish"` payload returned false instead of safe empty state | `src/documents/desktop_session_manager.cpp` | PASSED | PASSED |
| **DOC-02** | `stage227_23_adversarial_tests` | Malformed JSON strings (`""`, `"{"`) strictly rejected | `src/documents/desktop_session_manager.cpp` | PASSED | PASSED |
| **IPC-01** | `stage230_19_ipc_workflow_tests` | Recipient handle validity checking order relative to auto-revocation | `src/kernel/ipc_capability_broker.h` | PASSED | PASSED |
| **IPC-02** | `stage231_10_ipc_token_security_tests` | Auto-revocation check priority | `src/kernel/ipc_capability_broker.h` | PASSED | PASSED |
| **FS-01** | `stage233_11_hostile_filesystem_tests` | Relative path string concatenation escaping sandbox root | `src/packaging/canonical_path_validator.cpp` | PASSED | PASSED |
| **IPC-03** | `stage233_16_ipc_anti_replay_tests` | Sender validity checked prior to token revocation | `src/kernel/ipc_capability_broker.h` | PASSED | PASSED |

## Verification Conclusion
Zero remaining defects across both configurations. All historical tests pass with 100% assertion fidelity.
