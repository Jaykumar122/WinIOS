#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "memory/user_address_space.h"

namespace winios::memory {

enum class ValidationResult : std::uint32_t {
    Success = 0,
    NullPointer = 1,
    IntegerOverflow = 2,
    OutOfBounds = 3,
    UnmappedPage = 4,
    PermissionDenied = 5,
    MisalignedPointer = 6,
    CrossProcessViolation = 7,
    ZeroLengthForbidden = 8,
    StringNotTerminated = 9
};

inline const char* validation_result_to_string(ValidationResult res) noexcept {
    switch (res) {
    case ValidationResult::Success: return "Success";
    case ValidationResult::NullPointer: return "NullPointer";
    case ValidationResult::IntegerOverflow: return "IntegerOverflow";
    case ValidationResult::OutOfBounds: return "OutOfBounds";
    case ValidationResult::UnmappedPage: return "UnmappedPage";
    case ValidationResult::PermissionDenied: return "PermissionDenied";
    case ValidationResult::MisalignedPointer: return "MisalignedPointer";
    case ValidationResult::CrossProcessViolation: return "CrossProcessViolation";
    case ValidationResult::ZeroLengthForbidden: return "ZeroLengthForbidden";
    case ValidationResult::StringNotTerminated: return "StringNotTerminated";
    default: return "UnknownValidationResult";
    }
}

class GuestMemoryValidator {
public:
    // Validate an entire buffer range [va, va + size)
    static ValidationResult validate_buffer(
        const UserAddressSpace& uas,
        std::uint64_t va,
        std::size_t size,
        bool require_writable,
        std::size_t required_alignment = 1,
        bool allow_zero_length = false) noexcept;

    // Explicit guest read and write memory range validators
    static ValidationResult validate_guest_read(
        const UserAddressSpace& uas,
        std::uint64_t va,
        std::size_t size,
        std::size_t required_alignment = 1,
        bool allow_zero_length = false) noexcept {
        return validate_buffer(uas, va, size, false /* require_writable */, required_alignment, allow_zero_length);
    }

    static ValidationResult validate_guest_write(
        const UserAddressSpace& uas,
        std::uint64_t va,
        std::size_t size,
        std::size_t required_alignment = 1,
        bool allow_zero_length = false) noexcept {
        return validate_buffer(uas, va, size, true /* require_writable */, required_alignment, allow_zero_length);
    }

    // Safe copy in: Copy from guest user space into host buffer with full validation
    static ValidationResult copyin(
        const UserAddressSpace& uas,
        std::uint64_t user_va,
        void* host_dst,
        std::size_t size,
        std::size_t required_alignment = 1);

    // Safe copy out: Copy from host buffer into guest user space with full validation
    static ValidationResult copyout(
        UserAddressSpace& uas,
        const void* host_src,
        std::uint64_t user_va,
        std::size_t size,
        std::size_t required_alignment = 1);

    // Linux-style explicit aliases for copyin/copyout
    static ValidationResult copy_from_user(
        const UserAddressSpace& uas,
        std::uint64_t user_va,
        void* host_dst,
        std::size_t size,
        std::size_t required_alignment = 1) {
        return copyin(uas, user_va, host_dst, size, required_alignment);
    }

    static ValidationResult copy_to_user(
        UserAddressSpace& uas,
        const void* host_src,
        std::uint64_t user_va,
        std::size_t size,
        std::size_t required_alignment = 1) {
        return copyout(uas, host_src, user_va, size, required_alignment);
    }

    // Safe copy string: Copy null-terminated C string from guest user space with max length bound
    static ValidationResult copy_string(
        const UserAddressSpace& uas,
        std::uint64_t user_va,
        std::size_t max_bytes,
        std::string& out_str);
};

} // namespace winios::memory
