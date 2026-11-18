# WinIOS Campaign 219 — Stage 219.9: Guest Synchronization Architecture

## 1. Synchronization Primitives

Multi-threaded guest applications require mutual exclusion and coordination. WinIOS introduces lightweight, deterministic Category C synchronization system calls:

- **`SYS_winios_mutex_lock` (522)**:
  - Takes `arg0 = mutex_va` (virtual address of a 32-bit user memory word) and `arg1 = flags` (`0 = blocking`, `1 = trylock`).
  - Validates user memory buffer (4 bytes, 4-byte aligned, writable).
  - If memory word is 0 (unlocked), writes 1 (locked) atomically and returns 0 (success).
  - If memory word is 1 (locked) and trylock requested, returns `EBUSY` (16).
- **`SYS_winios_mutex_unlock` (523)**:
  - Takes `arg0 = mutex_va`.
  - Validates user memory buffer.
  - Writes 0 (unlocked) to user memory word and returns 0 (success).

## 2. Memory Validation & Safety

Every mutex operation strictly checks user address space boundaries using `uas.validate_user_buffer()`. If an application passes an unmapped or kernel-privileged pointer (`va >= 32 GiB`), the dispatcher rejects the syscall with `EFAULT` (14) and logs the violation, preventing kernel memory corruption.

## 3. Verification Evidence

Validated in `stage219_10_synchronization_tests.exe`:
- Mutex initialization to 0.
- First lock: word set to 1, return code 0.
- Contention detection: second lock attempt returns `EBUSY` (16).
- Mutex unlock: word reset to 0.
- Subsequent lock acquisition succeeds.
