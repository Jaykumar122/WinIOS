#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <algorithm>

namespace winios::kernel {

struct ProcessHandle {
    uint32_t pid{0};
    uint32_t generation{0};

    [[nodiscard]] constexpr uint64_t to_u64() const noexcept {
        return (static_cast<uint64_t>(generation) << 32) | static_cast<uint64_t>(pid);
    }

    [[nodiscard]] static constexpr ProcessHandle from_u64(uint64_t val) noexcept {
        return ProcessHandle{
            static_cast<uint32_t>(val & 0xFFFFFFFFULL),
            static_cast<uint32_t>((val >> 32) & 0xFFFFFFFFULL)
        };
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return pid != 0 && generation != 0;
    }

    constexpr bool operator==(const ProcessHandle& o) const noexcept {
        return pid == o.pid && generation == o.generation;
    }

    constexpr bool operator!=(const ProcessHandle& o) const noexcept {
        return !(*this == o);
    }
};

struct ApplicationIdentity {
    std::string bundle_id;
    std::string version{"1.0.0"};
    std::string display_name;
    uint32_t    min_sdk_version{0x00020300};
};

struct ProcessInstance {
    ProcessHandle       handle;
    uint64_t            instance_id{0}; // Monotonic process instantiation counter
    ApplicationIdentity app;
    uint64_t            start_time_us{0};
    bool                active{false};
};

class ProcessIdentityManager {
public:
    static constexpr uint32_t kMinUserPid = 100;
    static constexpr uint32_t kMaxUserPid = 32768;
    static constexpr uint32_t kMaxGeneration = 0xFFFFFFFFU;
    static constexpr size_t   kDefaultQuarantineDepth = 16;

    ProcessIdentityManager(uint32_t min_pid = kMinUserPid,
                           uint32_t max_pid = kMaxUserPid,
                           size_t quarantine_depth = kDefaultQuarantineDepth)
        : min_pid_(min_pid),
          max_pid_(max_pid),
          quarantine_depth_(quarantine_depth),
          next_candidate_pid_(min_pid) {}

    ~ProcessIdentityManager() = default;
    ProcessIdentityManager(const ProcessIdentityManager&) = delete;
    ProcessIdentityManager& operator=(const ProcessIdentityManager&) = delete;

    [[nodiscard]] ProcessHandle allocate_process(const ApplicationIdentity& app, uint64_t start_time_us = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t pid = find_available_pid_locked();
        if (pid == 0) {
            return ProcessHandle{0, 0}; // Exhausted
        }

        uint32_t gen = ++slot_generations_[pid];
        if (gen == 0 || gen == kMaxGeneration) {
            // Slot generation exhausted - retire permanently
            retired_pids_.push_back(pid);
            return allocate_process(app, start_time_us);
        }

        ProcessHandle handle{pid, gen};
        ProcessInstance inst{};
        inst.handle = handle;
        inst.instance_id = ++instance_counter_;
        inst.app = app;
        inst.start_time_us = start_time_us;
        inst.active = true;

        active_instances_[pid] = inst;
        return handle;
    }

    // Allocate a specific reserved PID (e.g. for fixed system daemons or backward compatibility test PIDs)
    [[nodiscard]] ProcessHandle allocate_fixed_pid(uint32_t pid, const ApplicationIdentity& app, uint64_t start_time_us = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pid == 0) return ProcessHandle{0, 0};
        if (active_instances_.find(pid) != active_instances_.end()) {
            return ProcessHandle{0, 0}; // Already in use
        }

        uint32_t gen = ++slot_generations_[pid];
        if (gen == 0) gen = 1;

        ProcessHandle handle{pid, gen};
        ProcessInstance inst{};
        inst.handle = handle;
        inst.instance_id = ++instance_counter_;
        inst.app = app;
        inst.start_time_us = start_time_us;
        inst.active = true;

        active_instances_[pid] = inst;
        return handle;
    }

    bool terminate_process(ProcessHandle handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_instances_.find(handle.pid);
        if (it == active_instances_.end()) {
            return false;
        }

        if (it->second.handle.generation != handle.generation) {
            return false; // Stale handle reference!
        }

        it->second.active = false;
        active_instances_.erase(it);

        // Place into quarantine queue to delay immediate reuse
        quarantine_queue_.push_back(handle.pid);
        trim_quarantine_locked();
        return true;
    }

    bool terminate_process_by_pid(uint32_t pid, uint32_t expected_generation = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_instances_.find(pid);
        if (it == active_instances_.end()) {
            return false;
        }

        if (expected_generation != 0 && it->second.handle.generation != expected_generation) {
            return false; // Stale generation mismatch
        }

        it->second.active = false;
        active_instances_.erase(it);

        quarantine_queue_.push_back(pid);
        trim_quarantine_locked();
        return true;
    }

    [[nodiscard]] bool is_valid_handle(ProcessHandle handle) const {
        if (!handle.is_valid()) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_instances_.find(handle.pid);
        if (it == active_instances_.end()) return false;
        return it->second.handle.generation == handle.generation && it->second.active;
    }

    [[nodiscard]] bool is_valid_pid(uint32_t pid) const {
        if (pid == 0) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_instances_.find(pid);
        return it != active_instances_.end() && it->second.active;
    }

    [[nodiscard]] std::optional<ProcessInstance> get_instance(ProcessHandle handle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_instances_.find(handle.pid);
        if (it == active_instances_.end()) return std::nullopt;
        if (it->second.handle.generation != handle.generation) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] std::optional<ProcessInstance> get_instance_by_pid(uint32_t pid) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_instances_.find(pid);
        if (it == active_instances_.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] uint32_t get_active_generation(uint32_t pid) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_instances_.find(pid);
        if (it != active_instances_.end()) return it->second.handle.generation;
        auto gen_it = slot_generations_.find(pid);
        return gen_it != slot_generations_.end() ? gen_it->second : 0;
    }

    [[nodiscard]] std::vector<ProcessInstance> list_active_instances() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ProcessInstance> result;
        result.reserve(active_instances_.size());
        for (const auto& [pid, inst] : active_instances_) {
            result.push_back(inst);
        }
        return result;
    }

    [[nodiscard]] size_t active_process_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_instances_.size();
    }

    void set_quarantine_depth(size_t depth) {
        std::lock_guard<std::mutex> lock(mutex_);
        quarantine_depth_ = depth;
        trim_quarantine_locked();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        active_instances_.clear();
        quarantine_queue_.clear();
        retired_pids_.clear();
        slot_generations_.clear();
        next_candidate_pid_ = min_pid_;
        instance_counter_ = 0;
    }

private:
    uint32_t find_available_pid_locked() {
        const uint32_t range_size = max_pid_ - min_pid_ + 1;
        for (uint32_t i = 0; i < range_size; ++i) {
            uint32_t candidate = next_candidate_pid_++;
            if (next_candidate_pid_ > max_pid_) {
                next_candidate_pid_ = min_pid_;
            }

            if (is_pid_available_locked(candidate)) {
                return candidate;
            }
        }

        // If all candidate PIDs are occupied or in quarantine, attempt draining quarantine if pool empty
        while (!quarantine_queue_.empty()) {
            uint32_t recycled = quarantine_queue_.front();
            quarantine_queue_.pop_front();
            if (is_pid_available_locked(recycled)) {
                return recycled;
            }
        }

        return 0; // Exhausted
    }

    bool is_pid_available_locked(uint32_t pid) const {
        if (active_instances_.find(pid) != active_instances_.end()) return false;
        if (std::find(retired_pids_.begin(), retired_pids_.end(), pid) != retired_pids_.end()) return false;
        if (std::find(quarantine_queue_.begin(), quarantine_queue_.end(), pid) != quarantine_queue_.end()) return false;
        return true;
    }

    void trim_quarantine_locked() {
        while (quarantine_queue_.size() > quarantine_depth_) {
            quarantine_queue_.pop_front();
        }
    }

    uint32_t                                  min_pid_;
    uint32_t                                  max_pid_;
    size_t                                    quarantine_depth_;
    uint32_t                                  next_candidate_pid_;
    uint64_t                                  instance_counter_{0};
    std::unordered_map<uint32_t, ProcessInstance> active_instances_;
    std::unordered_map<uint32_t, uint32_t>    slot_generations_;
    std::deque<uint32_t>                      quarantine_queue_;
    std::vector<uint32_t>                     retired_pids_;
    mutable std::mutex                        mutex_;
};

} // namespace winios::kernel
