# Stage 243.6: Syscall Event Capture and Replay Semantics

**Campaign**: 243
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Audited syscall recording and playback semantics:

- **Recorded Record**: Captures `syscall_num`, `arg0`, `arg1`, `arg2`, `ret_val`, `error_code`, and `carry_flag`.
- **Darwin ABI Error Encoding**: Captures whether Carry flag was asserted on failure.
- **Replay Enforcement**: Replaying a recorded syscall log against live execution verifies identical arguments, exit codes, and side-effects.
