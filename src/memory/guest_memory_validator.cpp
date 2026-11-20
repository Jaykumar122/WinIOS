#include "memory/guest_memory_validator.h"

#include <algorithm>
#include <cstring>

namespace winios::memory {

ValidationResult GuestMemoryValidator::validate_buffer(
    const UserAddressSpace& uas,
    std::uint64_t va,
    std::size_t size,
    bool require_writable,
    std::size_t required_alignment,
    bool allow_zero_length) noexcept {

    if (size == 0) {
        return allow_zero_length ? ValidationResult::Success : ValidationResult::ZeroLengthForbidden;
    }

    if (va == 0) {
        return ValidationResult::NullPointer;
    }

    if (required_alignment > 1 && (va % required_alignment != 0)) {
        return ValidationResult::MisalignedPointer;
    }

    if (va + size < va) {
        return ValidationResult::IntegerOverflow;
    }

    if (va + size > uas.user_space_limit()) {
        return ValidationResult::OutOfBounds;
    }

    std::uint64_t cur = va;
    const std::uint64_t end = va + size;

    while (cur < end) {
        PageDescriptorFields fields{};
        const std::uint64_t pa = uas.translate(cur, &fields);
        if (pa == 0 || !fields.valid || !fields.el0_accessible) {
            return ValidationResult::UnmappedPage;
        }
        if (require_writable && !fields.writable) {
            return ValidationResult::PermissionDenied;
        }

        const std::uint64_t next_page = (cur & ~(kPhysicalPageSize16KB - 1ULL)) + kPhysicalPageSize16KB;
        cur = (next_page < end) ? next_page : end;
    }

    return ValidationResult::Success;
}

ValidationResult GuestMemoryValidator::copyin(
    const UserAddressSpace& uas,
    std::uint64_t user_va,
    void* host_dst,
    std::size_t size,
    std::size_t required_alignment) {

    if (size == 0) {
        return ValidationResult::Success;
    }

    if (!host_dst) {
        return ValidationResult::NullPointer;
    }

    ValidationResult res = validate_buffer(uas, user_va, size, false, required_alignment, false);
    if (res != ValidationResult::Success) {
        return res;
    }

    auto* dst = static_cast<std::uint8_t*>(host_dst);
    std::size_t copied = 0;

    while (copied < size) {
        const std::uint64_t cur_va = user_va + copied;
        const std::uint64_t page_offset = cur_va & (kPhysicalPageSize16KB - 1ULL);
        const std::size_t chunk = std::min(size - copied, static_cast<std::size_t>(kPhysicalPageSize16KB - page_offset));

        const std::uint64_t pa = uas.translate(cur_va);
        if (pa == 0 || pa + chunk > uas.memory().size()) {
            return ValidationResult::UnmappedPage;
        }

        for (std::size_t i = 0; i < chunk; ++i) {
            dst[copied + i] = uas.memory().read_u8(static_cast<std::size_t>(pa + i));
        }

        copied += chunk;
    }

    return ValidationResult::Success;
}

ValidationResult GuestMemoryValidator::copyout(
    UserAddressSpace& uas,
    const void* host_src,
    std::uint64_t user_va,
    std::size_t size,
    std::size_t required_alignment) {

    if (size == 0) {
        return ValidationResult::Success;
    }

    if (!host_src) {
        return ValidationResult::NullPointer;
    }

    ValidationResult res = validate_buffer(uas, user_va, size, true, required_alignment, false);
    if (res != ValidationResult::Success) {
        return res;
    }

    const auto* src = static_cast<const std::uint8_t*>(host_src);
    std::size_t copied = 0;

    while (copied < size) {
        const std::uint64_t cur_va = user_va + copied;
        const std::uint64_t page_offset = cur_va & (kPhysicalPageSize16KB - 1ULL);
        const std::size_t chunk = std::min(size - copied, static_cast<std::size_t>(kPhysicalPageSize16KB - page_offset));

        const std::uint64_t pa = uas.translate(cur_va);
        if (pa == 0 || pa + chunk > uas.memory().size()) {
            return ValidationResult::UnmappedPage;
        }

        for (std::size_t i = 0; i < chunk; ++i) {
            uas.memory().write_u8(static_cast<std::size_t>(pa + i), src[copied + i]);
        }

        copied += chunk;
    }

    return ValidationResult::Success;
}

ValidationResult GuestMemoryValidator::copy_string(
    const UserAddressSpace& uas,
    std::uint64_t user_va,
    std::size_t max_bytes,
    std::string& out_str) {

    out_str.clear();

    if (user_va == 0) {
        return ValidationResult::NullPointer;
    }

    if (user_va >= kUserSpaceLimitVa) {
        return ValidationResult::OutOfBounds;
    }

    for (std::size_t i = 0; i < max_bytes; ++i) {
        const std::uint64_t cur_va = user_va + i;
        if (cur_va >= kUserSpaceLimitVa || cur_va < user_va) {
            out_str.clear();
            return ValidationResult::OutOfBounds;
        }

        PageDescriptorFields fields{};
        const std::uint64_t pa = uas.translate(cur_va, &fields);
        if (pa == 0 || !fields.valid || !fields.el0_accessible) {
            out_str.clear();
            return ValidationResult::UnmappedPage;
        }

        const std::uint8_t ch = uas.memory().read_u8(static_cast<std::size_t>(pa));
        if (ch == 0) {
            return ValidationResult::Success;
        }
        out_str.push_back(static_cast<char>(ch));
    }

    // Reached max_bytes without encountering null terminator: strictly reject
    out_str.clear();
    return ValidationResult::StringNotTerminated;
}

} // namespace winios::memory
