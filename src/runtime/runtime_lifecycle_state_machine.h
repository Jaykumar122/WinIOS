#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <optional>

namespace winios::runtime {

enum class AppLifecycleState : std::uint32_t {
    Created = 0,
    Admitted = 1,
    Loading = 2,
    Initialized = 3,
    Running = 4,
    Suspended = 5,
    Terminating = 6,
    Terminated = 7,
    Failed = 8
};

inline const char* app_lifecycle_state_to_string(AppLifecycleState state) noexcept {
    switch (state) {
    case AppLifecycleState::Created: return "Created";
    case AppLifecycleState::Admitted: return "Admitted";
    case AppLifecycleState::Loading: return "Loading";
    case AppLifecycleState::Initialized: return "Initialized";
    case AppLifecycleState::Running: return "Running";
    case AppLifecycleState::Suspended: return "Suspended";
    case AppLifecycleState::Terminating: return "Terminating";
    case AppLifecycleState::Terminated: return "Terminated";
    case AppLifecycleState::Failed: return "Failed";
    default: return "Unknown";
    }
}

struct LifecycleTransitionRecord {
    AppLifecycleState from_state;
    AppLifecycleState to_state;
    std::string       reason;
    std::uint64_t     timestamp_us{0};
};

class RuntimeLifecycleStateMachine {
public:
    explicit RuntimeLifecycleStateMachine(std::uint32_t process_id,
                                          std::uint32_t generation = 1,
                                          std::string bundle_id = "")
        : pid_(process_id),
          generation_(generation),
          bundle_id_(std::move(bundle_id)),
          current_state_(AppLifecycleState::Created) {}

    ~RuntimeLifecycleStateMachine() = default;

    [[nodiscard]] std::uint32_t process_id() const noexcept { return pid_; }
    [[nodiscard]] std::uint32_t generation() const noexcept { return generation_; }
    [[nodiscard]] const std::string& bundle_id() const noexcept { return bundle_id_; }

    [[nodiscard]] AppLifecycleState state() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_state_;
    }

    [[nodiscard]] bool is_terminal() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_state_ == AppLifecycleState::Terminated ||
               current_state_ == AppLifecycleState::Failed;
    }

    [[nodiscard]] bool can_transition_to(AppLifecycleState target) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_valid_transition_locked(current_state_, target);
    }

    bool transition_to(AppLifecycleState target, const std::string& reason = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_valid_transition_locked(current_state_, target)) {
            return false;
        }

        history_.push_back(LifecycleTransitionRecord{
            current_state_,
            target,
            reason,
            ++timestamp_counter_
        });

        current_state_ = target;
        return true;
    }

    // Atomically claim cleanup ownership to strictly prevent double cleanup / double free
    [[nodiscard]] bool begin_cleanup() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cleanup_claimed_) {
            return false;
        }
        cleanup_claimed_ = true;
        return true;
    }

    [[nodiscard]] bool is_cleanup_claimed() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return cleanup_claimed_;
    }

    [[nodiscard]] std::vector<LifecycleTransitionRecord> history() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return history_;
    }

private:
    static bool is_valid_transition_locked(AppLifecycleState from, AppLifecycleState to) noexcept {
        if (from == to) {
            return false;
        }

        switch (from) {
        case AppLifecycleState::Created:
            return (to == AppLifecycleState::Admitted || to == AppLifecycleState::Failed);

        case AppLifecycleState::Admitted:
            return (to == AppLifecycleState::Loading || to == AppLifecycleState::Failed);

        case AppLifecycleState::Loading:
            return (to == AppLifecycleState::Initialized || to == AppLifecycleState::Failed);

        case AppLifecycleState::Initialized:
            return (to == AppLifecycleState::Running ||
                    to == AppLifecycleState::Terminating ||
                    to == AppLifecycleState::Failed);

        case AppLifecycleState::Running:
            return (to == AppLifecycleState::Suspended ||
                    to == AppLifecycleState::Terminating ||
                    to == AppLifecycleState::Failed);

        case AppLifecycleState::Suspended:
            return (to == AppLifecycleState::Running ||
                    to == AppLifecycleState::Terminating ||
                    to == AppLifecycleState::Failed);

        case AppLifecycleState::Terminating:
            return (to == AppLifecycleState::Terminated);

        case AppLifecycleState::Terminated:
        case AppLifecycleState::Failed:
        default:
            // Terminal states admit zero forward transitions
            return false;
        }
    }

    const std::uint32_t pid_;
    const std::uint32_t generation_;
    const std::string bundle_id_;

    mutable std::mutex mutex_;
    AppLifecycleState current_state_{AppLifecycleState::Created};
    bool cleanup_claimed_{false};
    std::uint64_t timestamp_counter_{0};
    std::vector<LifecycleTransitionRecord> history_{};
};

} // namespace winios::runtime
