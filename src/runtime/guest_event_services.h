#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace winios::input {
class InputRouter;
}

namespace winios::runtime {

struct GuestTimer {
    std::uint32_t id{0};
    std::uint32_t owner_pid{0};
    std::uint32_t interval_ms{0};
    bool          is_periodic{false};
    std::uint64_t next_fire_time_ms{0};
    bool          active{false};
};

class GuestEventServices {
public:
    static constexpr std::size_t kMaxTimersPerProcess = 16;

    explicit GuestEventServices(std::shared_ptr<input::InputRouter> router = nullptr);
    ~GuestEventServices() = default;

    std::uint32_t create_timer(std::uint32_t pid, std::uint32_t interval_ms, bool is_periodic);
    bool cancel_timer(std::uint32_t pid, std::uint32_t timer_id);
    void cleanup_process_timers(std::uint32_t pid);

    // Deterministic virtual time step
    void step(std::uint32_t delta_ms);

    [[nodiscard]] std::size_t active_timer_count() const noexcept;
    [[nodiscard]] std::size_t active_timers_for_process(std::uint32_t pid) const noexcept;
    [[nodiscard]] std::uint64_t current_virtual_time_ms() const noexcept { return virtual_time_ms_; }

    void set_input_router(std::shared_ptr<input::InputRouter> router) { router_ = std::move(router); }

private:
    std::shared_ptr<input::InputRouter> router_;
    std::uint64_t                       virtual_time_ms_{0};
    std::uint32_t                       next_timer_id_{1};
    mutable std::mutex                  lock_;
    std::map<std::uint32_t, GuestTimer> timers_;
};

} // namespace winios::runtime
