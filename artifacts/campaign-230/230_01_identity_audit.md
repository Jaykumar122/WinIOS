# WinIOS Campaign 230 — Stage 230.1 Application Identity and PID Allocation Audit

## 1. Audit of Identity Across Campaigns 223–229
During Campaigns 223 through 229, guest applications and sample workloads operated with fixed, hardcoded PIDs in standalone simulation harnesses:
- **Campaign 227**: Text Editor ran as PID 95; Document Viewer ran as PID 96.
- **Campaign 228**: Settings App ran as PID 97; Task Monitor ran as PID 98.
- **Campaign 229**: Developer Diagnostics ran as PID 95; Lifecycle Demo ran as PID 96.

### Key Vulnerabilities & Risks Identified:
1. **Unsafe PID Reuse & Collision**:
   - PIDs 95 and 96 were reused across Campaign 227 and Campaign 229 without generation tracking at the supervisor level.
   - If a subsystem holds a lingering reference (e.g. document grant token, window ownership, IPC port, notification token) to `PID 95`, and that process terminates, a newly spawned process claiming `PID 95` would inadvertently inherit those permissions (Confused Deputy vulnerability).
2. **Conflation of Application vs Process Identity**:
   - In earlier iterations, `bundle_id` (e.g. `com.winios.settings`) was mapped directly 1:1 to a single integer PID. Multi-instance applications or relaunches could not distinguish between previous and new process instances.
3. **Generation Counter Exhaustion**:
   - In Campaign 229 `TaskManager`, `generation_counter_` was monotonically incremented, but lacked an explicit exhaustion policy or quarantine queue.

## 2. Hardened Process Identity Architecture (`src/kernel/process_identity.h`)

To remediate these issues, Stage 230.1 introduces `ProcessIdentityManager`:
1. **Three-Tier Identity Model**:
   - **Application Identity (`ApplicationIdentity`)**: Defines the software bundle (`bundle_id`, `version`, `display_name`, `min_sdk_version`).
   - **Application Instance (`ProcessInstance`)**: Tracks a specific instantiation event (`instance_id` monotonic 64-bit counter).
   - **Process Handle (`ProcessHandle`)**: 64-bit composite handle `(generation << 32) | pid`.
2. **Explicit Allocation & Quarantine Queue**:
   - Dynamic user PIDs allocated in `kMinUserPid = 100` to `kMaxUserPid = 32768`.
   - PIDs 0..99 are reserved for system daemons and fixed compatibility tests.
   - Upon process termination, the PID enters a FIFO quarantine queue (`kDefaultQuarantineDepth = 16`). A terminated PID is strictly prevented from immediate reallocation until it traverses the quarantine ring or the pool exhausts.
3. **Monotonic Generation & Stale Handle Rejection**:
   - Each PID slot maintains its own generation counter.
   - When a PID is recycled from quarantine, its generation increments.
   - Any API or service holding a `ProcessHandle` with an older generation will fail validation with `is_valid_handle()` -> `false`.
4. **Generation Exhaustion Policy**:
   - If a PID slot's generation reaches `0xFFFFFFFF`, the slot is retired permanently from allocation into `retired_pids_`, ensuring generation wrapping never allows a stale handle to collide.

## 3. Verification
- All test cases in `tests/unit/stage230_1_process_identity_tests.cpp` pass, verifying basic allocation, stale handle rejection, quarantine delay ring, PID reuse with incremented generation, fixed PID allocation for system apps, and multithreaded concurrency.
