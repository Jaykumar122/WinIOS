#pragma once

#include "devices/virtual_device_interface.h"

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <deque>

namespace winios::devices {

#pragma pack(push, 8)
struct AudioConfigData {
    std::uint32_t sample_rate{48000};
    std::uint16_t channels{2};
    std::uint16_t bits_per_sample{16};
    std::uint32_t buffer_size_bytes{4096};
    float         volume{1.0f};
    std::uint8_t  is_muted{0};
    std::uint8_t  pad[3]{0};
};

struct AudioStatusData {
    std::uint32_t state{0}; // DeviceState
    std::uint32_t sample_rate{48000};
    std::uint16_t channels{2};
    std::uint16_t bits_per_sample{16};
    std::uint32_t queued_bytes{0};
    std::uint32_t buffer_capacity{262144}; // 256 KB
    std::uint64_t total_frames_submitted{0};
    std::uint64_t total_bytes_submitted{0};
    std::uint64_t total_underruns{0};
    float         volume{1.0f};
    std::uint8_t  is_muted{0};
    std::uint8_t  pad[3]{0};
};
#pragma pack(pop)

class VirtualAudioService : public VirtualDeviceBase {
public:
    static constexpr std::uint32_t kDefaultAudioDeviceId = 4;
    static constexpr std::size_t   kMaxSubmissionBytes   = 65536;  // 64 KB per submit
    static constexpr std::size_t   kRingBufferCapacity   = 262144; // 256 KB

    VirtualAudioService();
    ~VirtualAudioService() override = default;

    // Configuration
    [[nodiscard]] AudioConfigData get_config() const;
    bool set_config(const AudioConfigData& cfg);

    // Audio stream control
    bool submit_audio(const std::uint8_t* pcm_data, std::size_t size_bytes);
    void simulate_playback(std::size_t bytes_to_drain);
    void flush();
    void set_volume(float vol);
    void set_muted(bool muted);

    // Status query
    [[nodiscard]] AudioStatusData get_status() const;
    [[nodiscard]] std::size_t queued_bytes() const;
    [[nodiscard]] std::uint64_t total_underruns() const;

    // Lifecycle overrides
    bool initialize() override;
    bool start() override;
    bool stop() override;
    bool suspend() override;
    bool resume() override;
    void reset() override;

private:
    mutable std::mutex lock_;
    AudioConfigData    config_{};
    std::deque<std::uint8_t> buffer_;
    std::uint64_t      total_frames_submitted_{0};
    std::uint64_t      total_bytes_submitted_{0};
    std::uint64_t      total_underruns_{0};
};

} // namespace winios::devices
