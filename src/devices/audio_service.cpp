#include "devices/audio_service.h"
#include "security/capability_manager.h"

#include <algorithm>
#include <cstring>

namespace winios::devices {

VirtualAudioService::VirtualAudioService()
    : VirtualDeviceBase(kDefaultAudioDeviceId,
                        1,
                        DeviceClass::AudioOutput,
                        "WinIOS Virtual Audio Service",
                        "WinIOS Audio Engine",
                        0x00010900,
                        static_cast<std::uint32_t>(security::Capability::AudioOutput)) {
    config_.sample_rate = 48000;
    config_.channels = 2;
    config_.bits_per_sample = 16;
    config_.buffer_size_bytes = 4096;
    config_.volume = 1.0f;
    config_.is_muted = 0;
}

AudioConfigData VirtualAudioService::get_config() const {
    std::lock_guard<std::mutex> guard(lock_);
    return config_;
}

bool VirtualAudioService::set_config(const AudioConfigData& cfg) {
    std::lock_guard<std::mutex> guard(lock_);
    if (cfg.sample_rate != 44100 && cfg.sample_rate != 48000) {
        return false;
    }
    if (cfg.channels != 1 && cfg.channels != 2) {
        return false;
    }
    if (cfg.bits_per_sample != 16) {
        return false;
    }
    config_ = cfg;
    config_.volume = std::clamp(cfg.volume, 0.0f, 1.0f);
    return true;
}

bool VirtualAudioService::submit_audio(const std::uint8_t* pcm_data, std::size_t size_bytes) {
    if (!pcm_data || size_bytes == 0 || size_bytes > kMaxSubmissionBytes) {
        return false;
    }

    std::lock_guard<std::mutex> guard(lock_);
    if (state_ != DeviceState::Running && state_ != DeviceState::Ready) {
        return false;
    }

    // Frame size = channels * (bits_per_sample / 8)
    const std::size_t bytes_per_frame = config_.channels * (config_.bits_per_sample / 8);
    if (bytes_per_frame == 0 || (size_bytes % bytes_per_frame) != 0) {
        return false;
    }

    // Check available space in ring buffer
    if (buffer_.size() + size_bytes > kRingBufferCapacity) {
        // Buffer overflow: drop or refuse
        return false;
    }

    buffer_.insert(buffer_.end(), pcm_data, pcm_data + size_bytes);
    total_bytes_submitted_ += size_bytes;
    total_frames_submitted_ += (size_bytes / bytes_per_frame);
    return true;
}

void VirtualAudioService::simulate_playback(std::size_t bytes_to_drain) {
    std::lock_guard<std::mutex> guard(lock_);
    if (bytes_to_drain == 0) {
        return;
    }

    if (buffer_.empty()) {
        if (state_ == DeviceState::Running) {
            ++total_underruns_;
        }
        return;
    }

    const std::size_t drain_count = std::min(bytes_to_drain, buffer_.size());
    buffer_.erase(buffer_.begin(), buffer_.begin() + drain_count);

    if (buffer_.empty() && bytes_to_drain > drain_count && state_ == DeviceState::Running) {
        ++total_underruns_;
    }
}

void VirtualAudioService::flush() {
    std::lock_guard<std::mutex> guard(lock_);
    buffer_.clear();
}

void VirtualAudioService::set_volume(float vol) {
    std::lock_guard<std::mutex> guard(lock_);
    config_.volume = std::clamp(vol, 0.0f, 1.0f);
}

void VirtualAudioService::set_muted(bool muted) {
    std::lock_guard<std::mutex> guard(lock_);
    config_.is_muted = muted ? 1 : 0;
}

AudioStatusData VirtualAudioService::get_status() const {
    std::lock_guard<std::mutex> guard(lock_);
    AudioStatusData status{};
    status.state = static_cast<std::uint32_t>(state_);
    status.sample_rate = config_.sample_rate;
    status.channels = config_.channels;
    status.bits_per_sample = config_.bits_per_sample;
    status.queued_bytes = static_cast<std::uint32_t>(buffer_.size());
    status.buffer_capacity = static_cast<std::uint32_t>(kRingBufferCapacity);
    status.total_frames_submitted = total_frames_submitted_;
    status.total_bytes_submitted = total_bytes_submitted_;
    status.total_underruns = total_underruns_;
    status.volume = config_.volume;
    status.is_muted = config_.is_muted;
    return status;
}

std::size_t VirtualAudioService::queued_bytes() const {
    std::lock_guard<std::mutex> guard(lock_);
    return buffer_.size();
}

std::uint64_t VirtualAudioService::total_underruns() const {
    std::lock_guard<std::mutex> guard(lock_);
    return total_underruns_;
}

bool VirtualAudioService::initialize() {
    std::lock_guard<std::mutex> guard(lock_);
    buffer_.clear();
    state_ = DeviceState::Ready;
    return true;
}

bool VirtualAudioService::start() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Ready || state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

bool VirtualAudioService::stop() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running || state_ == DeviceState::Suspended) {
        buffer_.clear();
        state_ = DeviceState::Stopped;
        return true;
    }
    return false;
}

bool VirtualAudioService::suspend() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running) {
        state_ = DeviceState::Suspended;
        return true;
    }
    return false;
}

bool VirtualAudioService::resume() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

void VirtualAudioService::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    buffer_.clear();
    total_frames_submitted_ = 0;
    total_bytes_submitted_ = 0;
    total_underruns_ = 0;
    state_ = DeviceState::Ready;
}

} // namespace winios::devices
