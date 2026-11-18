#pragma once

#include "platform/platform_error.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace winios::platform {

enum class ServiceState : uint32_t {
    Uninitialized = 0,
    Initializing  = 1,
    Running       = 2,
    Degraded      = 3,
    Stopping      = 4,
    Stopped       = 5,
    Failed        = 6
};

[[nodiscard]] constexpr const char* service_state_name(ServiceState state) noexcept {
    switch (state) {
        case ServiceState::Uninitialized: return "Uninitialized";
        case ServiceState::Initializing:  return "Initializing";
        case ServiceState::Running:       return "Running";
        case ServiceState::Degraded:      return "Degraded";
        case ServiceState::Stopping:      return "Stopping";
        case ServiceState::Stopped:       return "Stopped";
        case ServiceState::Failed:        return "Failed";
        default:                          return "Unknown";
    }
}

class IManagedService {
public:
    virtual ~IManagedService() = default;

    [[nodiscard]] virtual const std::string& service_name() const noexcept = 0;
    [[nodiscard]] virtual ServiceState service_state() const noexcept = 0;
    [[nodiscard]] virtual std::vector<std::string> service_dependencies() const = 0;

    virtual PlatformError service_init() = 0;
    virtual PlatformError service_shutdown() noexcept = 0;
};

class ServiceLifecycleTracker {
public:
    explicit ServiceLifecycleTracker(std::string name, std::vector<std::string> deps = {})
        : name_(std::move(name)), dependencies_(std::move(deps)) {}

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::vector<std::string>& dependencies() const noexcept { return dependencies_; }

    [[nodiscard]] ServiceState state() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    [[nodiscard]] const PlatformError& last_error() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_error_;
    }

    PlatformError begin_initialization() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == ServiceState::Running) {
            return PlatformError::ok(); // Idempotent
        }
        if (state_ == ServiceState::Initializing) {
            return PlatformError::conflict(PlatformDomain::Service, "Service is already initializing: " + name_);
        }
        state_ = ServiceState::Initializing;
        last_error_ = PlatformError::ok();
        return PlatformError::ok();
    }

    void complete_initialization(bool degraded = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = degraded ? ServiceState::Degraded : ServiceState::Running;
    }

    void fail_initialization(PlatformError err) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ServiceState::Failed;
        last_error_ = std::move(err);
    }

    PlatformError begin_shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == ServiceState::Stopped || state_ == ServiceState::Uninitialized) {
            return PlatformError::ok(); // Idempotent
        }
        state_ = ServiceState::Stopping;
        return PlatformError::ok();
    }

    void complete_shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ServiceState::Stopped;
    }

    void mark_degraded(PlatformError reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ServiceState::Degraded;
        last_error_ = std::move(reason);
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ServiceState::Uninitialized;
        last_error_ = PlatformError::ok();
    }

private:
    std::string              name_;
    std::vector<std::string> dependencies_;
    mutable std::mutex       mutex_;
    ServiceState             state_{ServiceState::Uninitialized};
    PlatformError            last_error_{};
};

class ServiceCoordinator {
public:
    ServiceCoordinator() = default;
    ~ServiceCoordinator() {
        shutdown_all();
    }

    PlatformError register_service(std::shared_ptr<IManagedService> svc) {
        if (!svc) {
            return PlatformError::invalid_argument(PlatformDomain::Service, "Null service pointer");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string name = svc->service_name();
        if (services_by_name_.find(name) != services_by_name_.end()) {
            return PlatformError::conflict(PlatformDomain::Service, "Duplicate service name: " + name);
        }
        services_by_name_[name] = svc;
        registration_order_.push_back(name);
        return PlatformError::ok();
    }

    PlatformError start_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> ordered;
        PlatformError order_err = resolve_startup_order_locked(ordered);
        if (!order_err.is_ok()) {
            return order_err;
        }

        std::vector<std::string> started;
        for (const auto& name : ordered) {
            auto svc = services_by_name_[name];
            PlatformError err = svc->service_init();
            if (!err.is_ok()) {
                // Partial initialization rollback: shutdown in reverse order
                for (auto it = started.rbegin(); it != started.rend(); ++it) {
                    services_by_name_[*it]->service_shutdown();
                }
                return PlatformError::internal_error(
                    PlatformDomain::Service,
                    "Failed to initialize service " + name + ": " + err.to_string());
            }
            started.push_back(name);
        }

        startup_order_ = std::move(ordered);
        return PlatformError::ok();
    }

    PlatformError shutdown_all() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        PlatformError last_err = PlatformError::ok();

        // Shutdown in reverse startup order
        if (!startup_order_.empty()) {
            for (auto it = startup_order_.rbegin(); it != startup_order_.rend(); ++it) {
                auto sit = services_by_name_.find(*it);
                if (sit != services_by_name_.end()) {
                    PlatformError err = sit->second->service_shutdown();
                    if (!err.is_ok() && last_err.is_ok()) {
                        last_err = err;
                    }
                }
            }
            startup_order_.clear();
        } else {
            for (auto it = registration_order_.rbegin(); it != registration_order_.rend(); ++it) {
                auto sit = services_by_name_.find(*it);
                if (sit != services_by_name_.end()) {
                    sit->second->service_shutdown();
                }
            }
        }
        return last_err;
    }

    PlatformError stop_all() noexcept {
        return shutdown_all();
    }

    [[nodiscard]] std::shared_ptr<IManagedService> get_service(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = services_by_name_.find(name);
        if (it != services_by_name_.end()) {
            return it->second;
        }
        return nullptr;
    }

    [[nodiscard]] size_t service_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return services_by_name_.size();
    }

private:
    PlatformError resolve_startup_order_locked(std::vector<std::string>& out_order) const {
        out_order.clear();
        std::unordered_map<std::string, int> in_degree;
        std::unordered_map<std::string, std::vector<std::string>> graph;

        for (const auto& [name, svc] : services_by_name_) {
            in_degree[name] = 0;
        }

        for (const auto& [name, svc] : services_by_name_) {
            for (const auto& dep : svc->service_dependencies()) {
                if (services_by_name_.find(dep) == services_by_name_.end()) {
                    return PlatformError::not_found(
                        PlatformDomain::Service,
                        "Unsatisfied dependency '" + dep + "' required by '" + name + "'");
                }
                graph[dep].push_back(name);
                in_degree[name]++;
            }
        }

        std::vector<std::string> queue;
        for (const auto& name : registration_order_) {
            if (in_degree[name] == 0) {
                queue.push_back(name);
            }
        }

        while (!queue.empty()) {
            std::string cur = queue.front();
            queue.erase(queue.begin());
            out_order.push_back(cur);

            for (const auto& dependent : graph[cur]) {
                in_degree[dependent]--;
                if (in_degree[dependent] == 0) {
                    queue.push_back(dependent);
                }
            }
        }

        if (out_order.size() != services_by_name_.size()) {
            return PlatformError::conflict(PlatformDomain::Service, "Circular dependency detected in service graph");
        }

        return PlatformError::ok();
    }

    mutable std::mutex                                         mutex_;
    std::unordered_map<std::string, std::shared_ptr<IManagedService>> services_by_name_;
    std::vector<std::string>                                   registration_order_;
    std::vector<std::string>                                   startup_order_;
};

} // namespace winios::platform
