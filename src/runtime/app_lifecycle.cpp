#include "runtime/app_lifecycle.h"
#include <algorithm>

namespace winios::runtime {

AppLifecycleManager::AppLifecycleManager(uint32_t process_id)
    : process_id_(process_id) {}

AppLifecycleManager::~AppLifecycleManager() {
    if (current_state_ != AppLifecycleState::kTerminated) {
        TransitionTo(AppLifecycleState::kTerminated);
    }
}

void AppLifecycleManager::RegisterObserver(AppLifecycleObserver* observer) {
    if (observer && std::find(observers_.begin(), observers_.end(), observer) == observers_.end()) {
        observers_.push_back(observer);
    }
}

void AppLifecycleManager::UnregisterObserver(AppLifecycleObserver* observer) {
    auto it = std::find(observers_.begin(), observers_.end(), observer);
    if (it != observers_.end()) {
        observers_.erase(it);
    }
}

bool AppLifecycleManager::TransitionTo(AppLifecycleState target_state) {
    if (current_state_ == target_state) return true;
    if (current_state_ == AppLifecycleState::kTerminated) return false; // Terminal state

    AppLifecycleState prev = current_state_;
    current_state_ = target_state;

    for (auto* obs : observers_) {
        if (!obs) continue;
        switch (target_state) {
        case AppLifecycleState::kLaunching:
            obs->OnLaunch();
            break;
        case AppLifecycleState::kActive:
            if (prev == AppLifecycleState::kBackgrounded) {
                obs->OnForeground();
            }
            obs->OnActive();
            break;
        case AppLifecycleState::kBackgrounded:
            obs->OnBackground();
            obs->OnSaveState();
            break;
        case AppLifecycleState::kTerminated:
            obs->OnSaveState();
            obs->OnTerminate();
            break;
        default:
            break;
        }
    }
    return true;
}

} // namespace winios::runtime
