#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

#include "winios_sdk/winios_sdk.h"

namespace winios::runtime {

enum class EventSourceProvenance : uint32_t {
    kPhysicalWindows = 1,
    kSyntheticInjected = 2,
    kKernelTimer = 3,
    kGuestLifecycle = 4
};

struct ProvenancedEvent {
    winios_event_t event;
    EventSourceProvenance provenance;
};

class GuestEventLoop {
public:
    static constexpr size_t kDefaultMaxQueueCapacity = 256;

    explicit GuestEventLoop(uint32_t process_id, size_t max_capacity = kDefaultMaxQueueCapacity);
    ~GuestEventLoop();

    bool PostEvent(const winios_event_t& ev, EventSourceProvenance prov = EventSourceProvenance::kSyntheticInjected);
    bool PollEvent(winios_event_t* ev_out, EventSourceProvenance* prov_out = nullptr);
    bool WaitEvent(winios_event_t* ev_out, uint32_t timeout_ms, EventSourceProvenance* prov_out = nullptr);

    void RequestExit(int exit_code = 0);
    bool IsExitRequested() const { return exit_requested_; }
    int  GetExitCode() const { return exit_code_; }

    size_t GetPendingCount() const;
    size_t GetDroppedCount() const { return dropped_events_count_; }
    void   Clear();

private:
    uint32_t process_id_{0};
    size_t   max_capacity_{kDefaultMaxQueueCapacity};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<ProvenancedEvent> queue_;
    bool exit_requested_{false};
    int  exit_code_{0};
    size_t dropped_events_count_{0};
};

} // namespace winios::runtime
