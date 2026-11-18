# WinIOS Campaign 225 — Stage 225.10 & 225.11 Virtual Audio Device Service

## 1. Audio Service Architecture
The `VirtualAudioService` ([src/devices/audio_service.h](file:///D:/WinISO26/WinIOS/src/devices/audio_service.h), [src/devices/audio_service.cpp](file:///D:/WinISO26/WinIOS/src/devices/audio_service.cpp)) emulates a low-latency digital audio output endpoint conforming to Apple Silicon audio pipeline characteristics.

### Supported Audio Formats:
- Linear PCM 16-bit little-endian signed integer samples.
- Channels: 2 (Stereo) or 1 (Mono).
- Nominal Sample Rates: 44,100 Hz (CD Audio) and 48,000 Hz (Standard Digital Media).
- Ring Buffer Capacity: 256 KB ($262,144$ bytes).

---

## 2. Audio Buffer Submission & Ring Buffer Management
Guest applications query audio status via Syscall 548 (`SYS_WINIOS_AUDIO_GET_STATUS`) and submit PCM audio buffers via Syscall 549 (`SYS_WINIOS_AUDIO_SUBMIT_BUFFER`).

### Submission Safety & Bounds:
1. **Single Submission Limit:** A single buffer submission is capped at 64 KB ($65,536$ bytes) to prevent starvation or blocking.
2. **Buffer Overflow Policy:** If the submitted data exceeds available ring buffer capacity, the device returns `WINIOS_STATUS_BUFFER_TOO_SMALL` / overflow code and increments `dropped_samples_count_`.
3. **Hardware Simulation Drain:** A deterministic `drain_samples(size_t byte_count)` routine models consumption by the host audio hardware, advancing playhead pointers and updating latency telemetry.
4. **Underrun Detection:** When playhead catches write head during active playback, an underrun counter is incremented, and status flags record the glitch condition for diagnostics.

---

## 3. Audio Status Descriptor (`winios_audio_status_t`)
```c
typedef struct {
    uint32_t state;             // 0: Idle, 1: Playing, 2: Paused, 3: Underrun
    uint32_t sample_rate;       // 44100 or 48000
    uint32_t channels;          // 1 or 2
    uint32_t buffer_capacity;   // Total ring buffer size (256 KB)
    uint32_t buffer_used;       // Current unrendered bytes in queue
    uint32_t underrun_count;    // Total underruns encountered
    uint32_t dropped_samples;   // Samples discarded due to buffer overflow
    float    volume;            // Master output volume (0.0 .. 1.0)
} winios_audio_status_t;
```

---

## 4. Verification Evidence
- [tests/unit/stage225_10_audio_service_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_10_audio_service_tests.cpp): Validates buffer enqueueing, drain simulation, volume scaling, overflow rejection, and underrun tracking.
