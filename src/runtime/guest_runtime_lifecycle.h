#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "runtime/guest_app_contract.h"
#include "runtime/guest_allocator.h"

namespace winios::runtime {

enum class RuntimeState : uint32_t {
    kUninitialized = 0,
    kStarting = 1,
    kRunning = 2,
    kTerminating = 3,
    kTerminated = 4,
    kFaulted = 5
};

struct GuestProcessConfig {
    uint32_t process_id{1};
    uint64_t app_base_address{0x100000000ULL};
    uint64_t entry_point{0x100001448ULL};
    uint64_t stack_base{0x7FFF0000ULL};
    uint64_t stack_size{64 * 1024};
    uint64_t heap_base{0x200000000ULL};
    uint64_t heap_size{4 * 1024 * 1024};
    size_t   heap_quota_bytes{2 * 1024 * 1024};
    std::string app_name{"App"};
};

class GuestRuntimeLifecycle {
public:
    explicit GuestRuntimeLifecycle(const GuestProcessConfig& config);
    ~GuestRuntimeLifecycle();

    bool Initialize(std::string* error_out = nullptr);
    bool Launch(std::string* error_out = nullptr);
    bool Step(uint64_t instruction_budget = 1000);
    bool Terminate(uint32_t exit_code = 0, GuestTerminationReason reason = GuestTerminationReason::kNormal);

    RuntimeState GetState() const { return state_; }
    GuestAppArgBlock GetArgBlock() const { return arg_block_; }
    GuestExitDescriptor GetExitDescriptor() const { return exit_desc_; }
    GuestAllocator* GetAllocator() { return allocator_.get(); }

    void RegisterAllocatedWindow(uint32_t wid);
    void RegisterAllocatedTimer(uint32_t timer_id);
    void RegisterOpenFileHandle(int fd);

    const std::vector<uint32_t>& GetAllocatedWindows() const { return allocated_windows_; }
    const std::vector<uint32_t>& GetAllocatedTimers() const { return allocated_timers_; }
    const std::vector<int>& GetOpenFileHandles() const { return open_file_handles_; }

    uint64_t GetRetiredInstructions() const { return retired_instructions_; }
    void IncrementRetiredInstructions(uint64_t count = 1) { retired_instructions_ += count; }

private:
    void TeardownResources();

    GuestProcessConfig config_;
    RuntimeState state_{RuntimeState::kUninitialized};
    GuestAppArgBlock arg_block_{};
    GuestExitDescriptor exit_desc_{};
    std::unique_ptr<GuestAllocator> allocator_;

    std::vector<uint32_t> allocated_windows_;
    std::vector<uint32_t> allocated_timers_;
    std::vector<int> open_file_handles_;
    uint64_t retired_instructions_{0};
};

} // namespace winios::runtime
