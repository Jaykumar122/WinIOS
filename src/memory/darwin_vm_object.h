#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace winios::memory {

// -----------------------------------------------------------------------------
// Darwin Architectural Virtual Memory Constants (16KB Page Granule for Apple Silicon)
// -----------------------------------------------------------------------------
inline constexpr std::size_t   kDarwinPageSize = 16384u; // 16 KB (0x4000)
inline constexpr std::uint64_t kDarwinPageMask = 0x3FFFULL;

inline constexpr std::uint32_t kDarwinVmProtNone    = 0x00u;
inline constexpr std::uint32_t kDarwinVmProtRead    = 0x01u;
inline constexpr std::uint32_t kDarwinVmProtWrite   = 0x02u;
inline constexpr std::uint32_t kDarwinVmProtExecute = 0x04u;
inline constexpr std::uint32_t kDarwinVmProtAll     = 0x07u;

// Standard Mach Return Codes
inline constexpr int kKernSuccess           = 0;
inline constexpr int kKernInvalidAddress    = 1;
inline constexpr int kKernProtectionFailure = 2;
inline constexpr int kKernNoSpace           = 3;
inline constexpr int kKernInvalidArgument   = 4;
inline constexpr int kKernFailure           = 5;
inline constexpr int kKernMemoryError       = 6;

// Darwin BSD mmap flags
inline constexpr std::uint32_t kDarwinMapShared    = 0x0001u;
inline constexpr std::uint32_t kDarwinMapPrivate   = 0x0002u;
inline constexpr std::uint32_t kDarwinMapFixed     = 0x0010u;
inline constexpr std::uint32_t kDarwinMapJit       = 0x0800u;
inline constexpr std::uint32_t kDarwinMapAnonymous = 0x1000u;

// Mach VM allocation flags (mach/vm_statistics.h)
inline constexpr int kDarwinVmFlagsFixed    = 0x0000;
inline constexpr int kDarwinVmFlagsAnywhere = 0x0001;

inline constexpr bool is_page_aligned(std::uint64_t addr) noexcept {
    return (addr & kDarwinPageMask) == 0;
}

inline constexpr std::uint64_t align_page_up(std::uint64_t size) noexcept {
    return (size + kDarwinPageMask) & ~kDarwinPageMask;
}

inline constexpr std::uint64_t align_page_down(std::uint64_t addr) noexcept {
    return addr & ~kDarwinPageMask;
}

// -----------------------------------------------------------------------------
// VM Object Mapping Types & Structure
// -----------------------------------------------------------------------------
enum class VmMappingType {
    Anonymous,
    FileBacked,
    Shared,
    CopyOnWrite
};

class DarwinVmObject {
public:
    DarwinVmObject(std::size_t size, VmMappingType type = VmMappingType::Anonymous, bool is_shared = false);
    ~DarwinVmObject() = default;

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] VmMappingType type() const noexcept { return type_; }
    [[nodiscard]] bool is_shared() const noexcept { return is_shared_; }
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }

    // Page access
    [[nodiscard]] bool read_bytes(std::size_t offset, void* dst, std::size_t length) const noexcept;
    [[nodiscard]] bool write_bytes(std::size_t offset, const void* src, std::size_t length) noexcept;

    // COW Fork / Shadow Clones
    [[nodiscard]] std::shared_ptr<DarwinVmObject> clone_for_cow() const;

private:
    std::uint64_t              id_{0};
    std::size_t                size_{0};
    VmMappingType              type_{VmMappingType::Anonymous};
    bool                       is_shared_{false};
    mutable std::vector<std::uint8_t> data_;
};

// -----------------------------------------------------------------------------
// VM Address Space Map Entry
// -----------------------------------------------------------------------------
struct DarwinVmEntry {
    std::uint64_t                   start_va{0};
    std::uint64_t                   size{0};
    std::size_t                     offset_in_object{0};
    std::uint32_t                   prot{kDarwinVmProtNone};
    std::uint32_t                   max_prot{kDarwinVmProtAll};
    std::uint32_t                   flags{0};
    bool                            is_cow{false};
    std::shared_ptr<DarwinVmObject> object;

    [[nodiscard]] std::uint64_t end_va() const noexcept { return start_va + size; }
};

// -----------------------------------------------------------------------------
// DarwinVmSpace: Comprehensive Synthetic VM Space per Process
// -----------------------------------------------------------------------------
class DarwinVmSpace {
public:
    explicit DarwinVmSpace(std::uint32_t pid, std::uint64_t base_hint = 0x0000000100000000ULL);
    ~DarwinVmSpace() = default;

    [[nodiscard]] std::uint32_t pid() const noexcept { return pid_; }

    // 1. Synthetic mmap & Object Mapping
    [[nodiscard]] std::uint64_t mmap(
        std::uint64_t addr,
        std::size_t length,
        std::uint32_t prot,
        std::uint32_t flags,
        int fd = -1,
        std::uint64_t offset = 0);

    [[nodiscard]] std::uint64_t map_object(
        std::shared_ptr<DarwinVmObject> object,
        std::uint64_t addr,
        std::size_t length,
        std::uint32_t prot,
        std::uint32_t flags,
        std::size_t offset = 0);

    // 2. Synthetic munmap (handles whole, partial edge, and middle split unmaps)
    [[nodiscard]] int munmap(std::uint64_t addr, std::size_t length);

    // 3. Synthetic mprotect (handles full and partial region protection changes)
    [[nodiscard]] int mprotect(std::uint64_t addr, std::size_t length, std::uint32_t new_prot);

    // 4. Mach VM API implementations
    [[nodiscard]] int mach_vm_allocate(std::uint64_t* address, std::size_t size, int flags);
    [[nodiscard]] int mach_vm_deallocate(std::uint64_t address, std::size_t size);
    [[nodiscard]] int mach_vm_protect(std::uint64_t address, std::size_t size, bool set_maximum, std::uint32_t new_prot);
    [[nodiscard]] int mach_vm_read(std::uint64_t address, void* buffer, std::size_t size) const;
    [[nodiscard]] int mach_vm_write(std::uint64_t address, const void* buffer, std::size_t size);

    // 5. Query and verification
    [[nodiscard]] bool check_access(std::uint64_t addr, std::size_t length, std::uint32_t required_prot) const noexcept;
    [[nodiscard]] const DarwinVmEntry* find_entry(std::uint64_t addr) const noexcept;
    [[nodiscard]] std::size_t region_count() const noexcept;
    [[nodiscard]] const std::vector<DarwinVmEntry>& entries() const noexcept { return entries_; }

    // 6. Fork / COW cloning: duplicates VM space where private writable regions become COW
    [[nodiscard]] std::shared_ptr<DarwinVmSpace> clone_for_fork(std::uint32_t new_pid) const;

private:
    [[nodiscard]] std::uint64_t find_free_range_locked(std::size_t length, std::uint64_t alignment);
    void split_entry_at_locked(std::size_t entry_index, std::uint64_t split_va);

    std::uint32_t               pid_{0};
    std::uint64_t               next_alloc_hint_{0x0000000100000000ULL};
    mutable std::mutex          lock_;
    std::vector<DarwinVmEntry>  entries_;
};

} // namespace winios::memory
