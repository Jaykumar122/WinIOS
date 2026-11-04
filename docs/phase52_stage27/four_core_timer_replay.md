# Four-Core Timer Replay

## 1. Virtual Timer Architecture

In WinIOS, each virtual core maintains independent architectural Generic Timer state within `VirtualTime`:
- Physical Timer: `CNTP_CTL_EL0`, `CNTP_CVAL_EL0`, `CNTP_TVAL_EL0`.
- Virtual Timer: `CNTV_CTL_EL0`, `CNTV_CVAL_EL0`, `CNTV_TVAL_EL0`, `CNTVOFF_EL2`.
- Hypervisor Physical Timer: `CNTHP_CTL_EL2`, `CNTHP_CVAL_EL2`, `CNTHP_TVAL_EL2`.

All timer progress is strictly synchronous with instruction retirement.

---

## 2. Four-Core Timer Replay Protocol

In `test_group_10_timer_replay`:
1. Sndl records `TimerComparatorExpired` events across cores 0, 1, 2, 3.
2. During replay, `process_pending_external_events()` consumes each timer record:
   ```cpp
   case sndl::SndlV2EventType::TimerComparatorExpired: {
       sndl::SndlV2Record consumed{};
       cursor_.consume_expected_type(rec.event_type, consumed);
       stats_.total_events_consumed++;
       break;
   }
   ```
3. Virtual time comparator matches generate deterministic PPI delivery without querying host system timers.
