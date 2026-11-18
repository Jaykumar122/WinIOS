#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <vector>

namespace winios::runtime {

enum class AppLifecycleState : uint32_t {
    kUnloaded = 0,
    kLaunching = 1,
    kActive = 2,
    kBackgrounded = 3,
    kTerminated = 4
};

class AppLifecycleObserver {
public:
    virtual ~AppLifecycleObserver() = default;
    virtual void OnLaunch() {}
    virtual void OnActive() {}
    virtual void OnBackground() {}
    virtual void OnForeground() {}
    virtual void OnTerminate() {}
    virtual void OnSaveState() {}
    virtual void OnRestoreState() {}
};

class AppLifecycleManager {
public:
    explicit AppLifecycleManager(uint32_t process_id);
    ~AppLifecycleManager();

    bool TransitionTo(AppLifecycleState target_state);
    AppLifecycleState GetCurrentState() const { return current_state_; }

    void RegisterObserver(AppLifecycleObserver* observer);
    void UnregisterObserver(AppLifecycleObserver* observer);

private:
    uint32_t process_id_{0};
    AppLifecycleState current_state_{AppLifecycleState::kUnloaded};
    std::vector<AppLifecycleObserver*> observers_;
};

} // namespace winios::runtime
