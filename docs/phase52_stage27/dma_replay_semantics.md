# DMA Replay Semantics & Blocker Status (BLK-SMP-03)

## 1. Blocker Status: BLK-SMP-03

Because the current WinIOS architecture implements no DMA-capable virtual peripherals:
1. **DMA replay remains unsupported**.
2. **BLK-SMP-03 remains an active blocker**, safeguarded by explicit capability gate detection.
3. No fabricated DMA implementations or benchmark numbers are reported.

---

## 2. Replay Safety & Rejection Policy

In `SmpReplayCoordinator::prepare()`:
```cpp
if (config_.allow_uncaptured_dma) {
    state_ = SmpReplayState::Unsupported;
    return ReplayResult::fail(
        ReplayErrorCode::UnsupportedConfiguration,
        "BLK-SMP-03 violation: uncaptured host DMA replay is unsupported."
    );
}
```

In `SmpReplayCoordinator::process_pending_external_events()`:
```cpp
case sndl::SndlV2EventType::HostInputInjected: {
    if (!config_.allow_uncaptured_dma) {
        SmpReplayDivergence d;
        d.type = SmpReplayDivergenceType::UnsupportedExternalInput;
        d.message = "HostInputInjected encountered but BLK-SMP-03 host DMA is not enabled in capability gate";
        trigger_divergence(d);
        return false;
    }
}
```

Any attempt to replay an execution requiring uncaptured DMA fails cleanly before or at the moment of injection, guaranteeing that zero unrecorded host memory mutations can silently corrupt guest execution.
