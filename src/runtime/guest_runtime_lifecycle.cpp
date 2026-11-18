#include "runtime/guest_runtime_lifecycle.h"

namespace winios::runtime {

GuestRuntimeLifecycle::GuestRuntimeLifecycle(const GuestProcessConfig& config)
    : config_(config) {}

GuestRuntimeLifecycle::~GuestRuntimeLifecycle() {
    if (state_ != RuntimeState::kTerminated) {
        Terminate(1, GuestTerminationReason::kKilled);
    }
}

bool GuestRuntimeLifecycle::Initialize(std::string* error_out) {
    if (state_ != RuntimeState::kUninitialized) {
        if (error_out) *error_out = "Runtime already initialized";
        return false;
    }

    state_ = RuntimeState::kStarting;

    // Validate stack alignment (16-byte aligned)
    if (!GuestAppContract::ValidateStackAlignment(config_.stack_base)) {
        state_ = RuntimeState::kFaulted;
        if (error_out) *error_out = "Stack pointer not 16-byte aligned";
        return false;
    }

    // Set up arg block
    arg_block_ = GuestAppContract::CreateDefault(
        config_.process_id,
        config_.app_base_address,
        config_.stack_base,
        config_.stack_size,
        config_.heap_base,
        config_.heap_size);

    std::string err;
    if (!GuestAppContract::ValidateArgBlock(arg_block_, &err)) {
        state_ = RuntimeState::kFaulted;
        if (error_out) *error_out = err;
        return false;
    }

    // Initialize allocator
    allocator_ = std::make_unique<GuestAllocator>(
        config_.heap_base,
        static_cast<size_t>(config_.heap_size),
        config_.heap_quota_bytes,
        config_.process_id);

    return true;
}

bool GuestRuntimeLifecycle::Launch(std::string* error_out) {
    if (state_ != RuntimeState::kStarting) {
        if (error_out) *error_out = "Runtime must be in Starting state to launch";
        return false;
    }
    state_ = RuntimeState::kRunning;
    return true;
}

bool GuestRuntimeLifecycle::Step(uint64_t instruction_budget) {
    if (state_ != RuntimeState::kRunning) return false;
    retired_instructions_ += instruction_budget;
    return true;
}

bool GuestRuntimeLifecycle::Terminate(uint32_t exit_code, GuestTerminationReason reason) {
    if (state_ == RuntimeState::kTerminated) return true;
    state_ = RuntimeState::kTerminating;

    exit_desc_.exit_code = exit_code;
    exit_desc_.termination_reason = static_cast<uint32_t>(reason);
    exit_desc_.retired_instructions = retired_instructions_;
    exit_desc_.elapsed_us = 1000; // Simulated execution time

    TeardownResources();
    state_ = RuntimeState::kTerminated;
    return true;
}

void GuestRuntimeLifecycle::RegisterAllocatedWindow(uint32_t wid) {
    allocated_windows_.push_back(wid);
}

void GuestRuntimeLifecycle::RegisterAllocatedTimer(uint32_t timer_id) {
    allocated_timers_.push_back(timer_id);
}

void GuestRuntimeLifecycle::RegisterOpenFileHandle(int fd) {
    open_file_handles_.push_back(fd);
}

void GuestRuntimeLifecycle::TeardownResources() {
    // Release resources tracked by process
    allocated_windows_.clear();
    allocated_timers_.clear();
    open_file_handles_.clear();
    if (allocator_) {
        allocator_->Reset();
    }
}

} // namespace winios::runtime
