#pragma once

#include "trace_ring_buffer.h"
#include "trace_serializer.h"
#include "trace_types.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace winios::trace {

// -----------------------------------------------------------------------------
// EventTypeFilter: Type-safe 256-bit bitset for TraceEventType filtering
// -----------------------------------------------------------------------------
class EventTypeFilter {
public:
    constexpr EventTypeFilter() = default;

    void set_all(bool enabled = true) noexcept {
        if (enabled) {
            mask_.set();
        } else {
            mask_.reset();
        }
    }

    void reset() noexcept {
        mask_.reset();
    }

    void set(TraceEventType type, bool enabled = true) noexcept {
        const auto idx = static_cast<std::size_t>(type);
        if (idx < mask_.size()) {
            mask_.set(idx, enabled);
        }
    }

    void set(std::uint16_t type, bool enabled = true) noexcept {
        if (type < mask_.size()) {
            mask_.set(type, enabled);
        }
    }

    [[nodiscard]] bool test(TraceEventType type) const noexcept {
        const auto idx = static_cast<std::size_t>(type);
        if (idx < mask_.size()) {
            return mask_.test(idx);
        }
        return false;
    }

    [[nodiscard]] bool test(std::uint16_t type) const noexcept {
        if (type < mask_.size()) {
            return mask_.test(type);
        }
        return false;
    }

    [[nodiscard]] bool any() const noexcept {
        return mask_.any();
    }

    [[nodiscard]] bool none() const noexcept {
        return mask_.none();
    }

    [[nodiscard]] std::size_t count() const noexcept {
        return mask_.count();
    }

    // Helper to populate from standard 64-bit mask for low architectural types (0..63)
    void from_u64_mask(std::uint64_t mask) noexcept {
        mask_.reset();
        for (std::size_t i = 0; i < 64; ++i) {
            if ((mask & (1ULL << i)) != 0) {
                mask_.set(i, true);
            }
        }
    }

    [[nodiscard]] std::uint64_t to_u64_mask() const noexcept {
        std::uint64_t result = 0;
        for (std::size_t i = 0; i < 64; ++i) {
            if (mask_.test(i)) {
                result |= (1ULL << i);
            }
        }
        return result;
    }

    bool operator==(const EventTypeFilter& other) const noexcept {
        return mask_ == other.mask_;
    }

    bool operator!=(const EventTypeFilter& other) const noexcept {
        return mask_ != other.mask_;
    }

private:
    std::bitset<256> mask_{};
};

// -----------------------------------------------------------------------------
// TraceConfigError: Explicit validation errors for runtime configuration
// -----------------------------------------------------------------------------
enum class TraceConfigError : std::uint32_t {
    Success                 = 0,
    InvalidRingCapacity     = 1,
    InvalidBatchSize        = 2,
    EmptyEventTypeFilter    = 3,
    EmptyCoreMask           = 4,
    EmptyOutputPath         = 5,
    InvalidOutputPath       = 6
};

constexpr inline std::string_view to_string(TraceConfigError err) noexcept {
    switch (err) {
        case TraceConfigError::Success:              return "Success";
        case TraceConfigError::InvalidRingCapacity:  return "InvalidRingCapacity";
        case TraceConfigError::InvalidBatchSize:     return "InvalidBatchSize";
        case TraceConfigError::EmptyEventTypeFilter: return "EmptyEventTypeFilter";
        case TraceConfigError::EmptyCoreMask:        return "EmptyCoreMask";
        case TraceConfigError::EmptyOutputPath:      return "EmptyOutputPath";
        case TraceConfigError::InvalidOutputPath:    return "InvalidOutputPath";
        default:                                     return "UnknownError";
    }
}

// -----------------------------------------------------------------------------
// TraceConfig: Runtime trace configuration model
// -----------------------------------------------------------------------------
struct TraceConfig {
    bool enabled{false};
    bool file_output_enabled{false};
    bool per_core_filtering{false};

    std::size_t ring_capacity{DEFAULT_RING_BUFFER_CAPACITY};
    std::size_t writer_batch_size{1024};

    EventTypeFilter event_types{};
    std::uint64_t core_mask{~std::uint64_t{0}}; // All cores 0..63 enabled by default

    std::filesystem::path output_path{};

    // Helper: default configuration with architectural events enabled
    static TraceConfig default_enabled_config() {
        TraceConfig cfg;
        cfg.enabled = true;
        cfg.file_output_enabled = false;
        // Enable standard architectural events (1..13) and diagnostic markers (200..202)
        cfg.event_types.set(TraceEventType::InstructionRetired, true);
        cfg.event_types.set(TraceEventType::BranchTaken, true);
        cfg.event_types.set(TraceEventType::ExceptionEntered, true);
        cfg.event_types.set(TraceEventType::ExceptionReturned, true);
        cfg.event_types.set(TraceEventType::InterruptDelivered, true);
        cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
        cfg.event_types.set(TraceEventType::MemoryRead, true);
        cfg.event_types.set(TraceEventType::MemoryWrite, true);
        cfg.event_types.set(TraceEventType::DebugEvent, true);
        cfg.event_types.set(TraceEventType::CPUStarted, true);
        cfg.event_types.set(TraceEventType::CPUStopped, true);
        cfg.event_types.set(TraceEventType::ContextChanged, false);
        cfg.event_types.set(TraceEventType::TimerEvent, false);
        cfg.event_types.set(TraceEventType::DroppedMarker, true);
        cfg.event_types.set(TraceEventType::TraceStart, true);
        cfg.event_types.set(TraceEventType::TraceEnd, true);
        return cfg;
    }

    [[nodiscard]] TraceConfigError validate() const noexcept {
        if (!enabled) {
            // Disabled configuration is always valid
            return TraceConfigError::Success;
        }

        if (ring_capacity < MIN_RING_BUFFER_CAPACITY || ring_capacity > MAX_RING_BUFFER_CAPACITY) {
            return TraceConfigError::InvalidRingCapacity;
        }

        if (writer_batch_size == 0 || writer_batch_size > MAX_EVENTS_PER_BLOCK) {
            return TraceConfigError::InvalidBatchSize;
        }

        if (core_mask == 0) {
            return TraceConfigError::EmptyCoreMask;
        }

        if (!event_types.any()) {
            return TraceConfigError::EmptyEventTypeFilter;
        }

        if (file_output_enabled) {
            if (writer_batch_size > ring_capacity) {
                return TraceConfigError::InvalidBatchSize;
            }
            if (output_path.empty()) {
                return TraceConfigError::EmptyOutputPath;
            }
            if (output_path.filename().empty()) {
                return TraceConfigError::InvalidOutputPath;
            }
        }

        return TraceConfigError::Success;
    }

    [[nodiscard]] bool is_valid() const noexcept {
        return validate() == TraceConfigError::Success;
    }
};

} // namespace winios::trace
