#include "runtime/guest_app_contract.h"

namespace winios::runtime {

bool GuestAppContract::ValidateArgBlock(const GuestAppArgBlock& block, std::string* error_out) {
    if (block.contract_version < kGuestAppMinSupportedContract ||
        block.contract_version > kGuestAppContractVersion) {
        if (error_out) *error_out = "Unsupported contract version";
        return false;
    }
    if (block.process_id == 0) {
        if (error_out) *error_out = "Invalid process ID 0";
        return false;
    }
    if (block.stack_base == 0 || block.stack_size == 0) {
        if (error_out) *error_out = "Invalid stack geometry";
        return false;
    }
    if ((block.stack_base & 0xF) != 0) {
        if (error_out) *error_out = "Stack base must be 16-byte aligned";
        return false;
    }
    if (block.heap_base != 0 && block.heap_limit <= block.heap_base) {
        if (error_out) *error_out = "Invalid heap bounds";
        return false;
    }
    return true;
}

bool GuestAppContract::NegotiateAbiVersion(uint32_t requested_version, uint32_t* negotiated_version) {
    if (!negotiated_version) return false;
    if (requested_version > kGuestAppContractVersion) {
        *negotiated_version = kGuestAppContractVersion;
        return true;
    }
    if (requested_version >= kGuestAppMinSupportedContract) {
        *negotiated_version = requested_version;
        return true;
    }
    return false;
}

bool GuestAppContract::ValidateStackAlignment(uint64_t sp) {
    return (sp & 0xF) == 0;
}

GuestAppArgBlock GuestAppContract::CreateDefault(uint32_t pid, uint64_t app_base, uint64_t stack_base,
                                                uint64_t stack_sz, uint64_t heap_base, uint64_t heap_sz) {
    GuestAppArgBlock b{};
    b.contract_version = kGuestAppContractVersion;
    b.process_id = pid;
    b.app_base_address = app_base;
    b.stack_base = stack_base;
    b.stack_size = stack_sz;
    b.heap_base = heap_base;
    b.heap_limit = heap_base + heap_sz;
    b.argc = 0;
    b.envc = 0;
    b.argv_ptrs = 0;
    b.envp_ptrs = 0;
    b.initial_window_id = 0;
    b.bundle_storage_id = pid;
    return b;
}

} // namespace winios::runtime
