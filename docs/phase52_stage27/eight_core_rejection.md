# Eight-Core Replay Rejection Specification

## 1. Explicit Rejection Invariant

In accordance with Stage 27 constraints:
> "Do not enable eight-core replay in this stage. Keep eight-core replay rejected with `ReplayErrorCode::UnsupportedConfiguration`."

The capability gate and replay session strictly reject 8-core execution across all layers.

---

## 2. Layered Rejection Points

1. **Snapshot Core Count Rejection**:
   In `SmpReplayCoordinator::prepare()`:
   ```cpp
   if (snap_cores == 8) {
       state_ = SmpReplayState::Unsupported;
       return ReplayResult::fail(
           ReplayErrorCode::UnsupportedConfiguration,
           "Eight-core SMP replay is strictly rejected by capability gate."
       );
   }
   ```

2. **SNDL Recording Core Count Rejection**:
   ```cpp
   if (cursor_.header().core_count == 8) {
       state_ = SmpReplayState::Unsupported;
       return ReplayResult::fail(
           ReplayErrorCode::UnsupportedConfiguration,
           "Eight-core SMP recording is strictly rejected by capability gate."
       );
   }
   ```

3. **ReplaySession Dispatch Rejection**:
   In `ReplaySession::prepare()`:
   ```cpp
   if (cc == 8) {
       state_ = ReplayLifecycleState::Failed;
       return ReplayResult::fail(
           ReplayErrorCode::UnsupportedConfiguration,
           "Eight-core SMP replay is strictly rejected by capability gate."
       );
   }
   ```

No resource allocation or partial cluster initialization takes place before rejection.
