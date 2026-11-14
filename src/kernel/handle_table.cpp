#include "kernel/handle_table.h"

namespace winios::kernel {

KernelObject::KernelObject(KernelObjectType type, std::uint64_t id)
    : type_(type), id_(id) {}

HandleTable::HandleTable(std::size_t max_handles)
    : max_handles_(max_handles > 0 ? max_handles : 1024) {
    entries_.resize(1); // 0 is invalid handle
}

std::int32_t HandleTable::allocate_handle(std::shared_ptr<KernelObject> object, std::uint32_t rights) {
    if (!object) {
        return -1;
    }

    std::int32_t handle_idx = -1;
    if (!free_list_.empty()) {
        handle_idx = free_list_.back();
        free_list_.pop_back();
        entries_[static_cast<std::size_t>(handle_idx)] = HandleEntry{object, rights, true};
    } else {
        if (entries_.size() > max_handles_) {
            return -1; // Handle table exhausted
        }
        handle_idx = static_cast<std::int32_t>(entries_.size());
        entries_.push_back(HandleEntry{object, rights, true});
    }

    object->add_ref();
    ++active_count_;
    return handle_idx;
}

std::shared_ptr<KernelObject> HandleTable::get_object(std::int32_t handle, std::uint32_t required_rights) const {
    if (!is_valid_handle(handle)) {
        return nullptr;
    }

    const auto& entry = entries_[static_cast<std::size_t>(handle)];
    if ((entry.rights & required_rights) != required_rights) {
        return nullptr; // Rights mismatch
    }

    return entry.object;
}

std::int32_t HandleTable::duplicate_handle(std::int32_t handle, std::uint32_t new_rights_mask) {
    if (!is_valid_handle(handle)) {
        return -1;
    }

    const auto& entry = entries_[static_cast<std::size_t>(handle)];
    if ((entry.rights & HandleRights::Duplicate) == 0) {
        return -1; // Lacks duplicate permission
    }

    // Attenuate rights: new handle cannot have rights the parent didn't have
    const std::uint32_t attenuated_rights = entry.rights & new_rights_mask;
    return allocate_handle(entry.object, attenuated_rights);
}

bool HandleTable::close_handle(std::int32_t handle) {
    if (!is_valid_handle(handle)) {
        return false;
    }

    auto& entry = entries_[static_cast<std::size_t>(handle)];
    if (entry.object) {
        entry.object->release();
    }
    entry.object = nullptr;
    entry.rights = 0;
    entry.valid = false;

    free_list_.push_back(handle);
    --active_count_;
    return true;
}

bool HandleTable::is_valid_handle(std::int32_t handle) const noexcept {
    if (handle <= 0 || static_cast<std::size_t>(handle) >= entries_.size()) {
        return false;
    }
    return entries_[static_cast<std::size_t>(handle)].valid;
}

std::uint32_t HandleTable::get_rights(std::int32_t handle) const noexcept {
    if (!is_valid_handle(handle)) {
        return 0;
    }
    return entries_[static_cast<std::size_t>(handle)].rights;
}

} // namespace winios::kernel
