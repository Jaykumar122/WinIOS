#include <iostream>
#include <cstdint>
#include <vector>

#include "devices/audio_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::devices;

int main() {
    std::cout << "[Test Stage 225.10] Virtual Audio Device & Underrun Recovery...\n" << std::flush;

    VirtualAudioService audio;
    TEST_ASSERT(audio.initialize());
    TEST_ASSERT(audio.start());

    // 1. Config tests
    AudioConfigData cfg = audio.get_config();
    TEST_ASSERT(cfg.sample_rate == 48000);
    TEST_ASSERT(cfg.channels == 2);
    TEST_ASSERT(cfg.bits_per_sample == 16);

    // Set 44.1 kHz stereo
    cfg.sample_rate = 44100;
    TEST_ASSERT(audio.set_config(cfg));
    TEST_ASSERT(audio.get_config().sample_rate == 44100);

    // Reject bad config
    cfg.sample_rate = 96000; // Unsupported
    TEST_ASSERT(!audio.set_config(cfg));

    // 2. Buffer submission
    // 44100 Hz stereo 16-bit = 4 bytes per frame
    std::vector<uint8_t> pcm_chunk(4096, 0x11);
    TEST_ASSERT(audio.submit_audio(pcm_chunk.data(), pcm_chunk.size()));
    TEST_ASSERT(audio.queued_bytes() == 4096);

    auto st = audio.get_status();
    TEST_ASSERT(st.total_frames_submitted == 1024);
    TEST_ASSERT(st.total_bytes_submitted == 4096);
    TEST_ASSERT(st.queued_bytes == 4096);

    // Reject unaligned size (not multiple of 4 bytes)
    std::vector<uint8_t> bad_chunk(17, 0x22);
    TEST_ASSERT(!audio.submit_audio(bad_chunk.data(), bad_chunk.size()));

    // Reject oversized submission (> 64 KB)
    std::vector<uint8_t> big_chunk(65536 + 4, 0x33);
    TEST_ASSERT(!audio.submit_audio(big_chunk.data(), big_chunk.size()));

    // 3. Playback simulation and underrun detection
    audio.simulate_playback(2048);
    TEST_ASSERT(audio.queued_bytes() == 2048);
    TEST_ASSERT(audio.total_underruns() == 0);

    // Drain remaining 2048 plus extra -> triggers underrun
    audio.simulate_playback(4096);
    TEST_ASSERT(audio.queued_bytes() == 0);
    TEST_ASSERT(audio.total_underruns() == 1);

    // Draining empty buffer while running triggers another underrun
    audio.simulate_playback(1024);
    TEST_ASSERT(audio.total_underruns() == 2);

    // 4. Underrun recovery: submitting more audio restores queue
    TEST_ASSERT(audio.submit_audio(pcm_chunk.data(), pcm_chunk.size()));
    TEST_ASSERT(audio.queued_bytes() == 4096);

    // 5. Volume and mute
    audio.set_volume(0.75f);
    TEST_ASSERT(audio.get_status().volume == 0.75f);
    audio.set_volume(2.5f); // Clamped to 1.0
    TEST_ASSERT(audio.get_status().volume == 1.0f);
    audio.set_muted(true);
    TEST_ASSERT(audio.get_status().is_muted == 1);

    audio.flush();
    TEST_ASSERT(audio.queued_bytes() == 0);

    std::cout << "[Test Stage 225.10] PASS: Audio service verified.\n";
    return 0;
}
