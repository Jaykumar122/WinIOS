#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
#include "memory/xnu_vm_map.h"

namespace winios::memory {

inline constexpr std::uint64_t kUserSpaceBaseVa = 0x0000000000000000ULL;
inline constexpr std::uint64_t kUserSpaceLimitVa = 0x0000000080000000ULL; // 2 GiB user address space limit

class UserAddressSpace {
public:
    UserAddressSpace(Memory& mem,
                     PhysicalPageManager& ppm,
                     std::uint64_t root_l2_table_pa,
                     std::string name = "user_space");
    ~UserAddressSpace();

    [[nodiscard]] std::uint64_t root_table_pa() const noexcept { return root_l2_table_pa_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] Memory& memory() noexcept { return mem_; }
    [[nodiscard]] const Memory& memory() const noexcept { return mem_; }
    [[nodiscard]] XnuVmMap& vm_map() noexcept { return vm_map_; }
    [[nodiscard]] const XnuVmMap& vm_map() const noexcept { return vm_map_; }

    // Map a single 16KB user page
    bool map_user_page(std::uint64_t va,
                       std::uint64_t pa,
                       PagePermissions perms);

    // Map a contiguous range of 16KB user pages
    bool map_user_range(std::uint64_t start_va,
                        std::uint64_t start_pa,
                        std::size_t num_pages,
                        PagePermissions perms);

    // Unmap a single user page
    bool unmap_user_page(std::uint64_t va);

    // Translate virtual address through Stage-1 user page table
    [[nodiscard]] std::uint64_t translate(std::uint64_t va,
                                          PageDescriptorFields* out_fields = nullptr) const;

    // Validates that a user memory buffer [va, va + size) is mapped and satisfies required permissions
    [[nodiscard]] bool validate_user_buffer(std::uint64_t va,
                                            std::size_t size,
                                            bool require_writable) const;

    // Allocate and map user stack in guest memory
    bool allocate_user_stack(std::uint64_t stack_top_va,
                             std::size_t stack_size,
                             std::uint64_t& out_stack_bottom_va);

    // Track a physical page owned by this address space
    void record_allocated_page(std::uint64_t pa);
    [[nodiscard]] const std::vector<std::uint64_t>& allocated_pages() const noexcept { return allocated_physical_pages_; }


    // Copy user data into address space
    bool copyin(std::uint64_t dst_user_va, const void* src_host, std::size_t len);

    // Copy user data out of address space
    bool copyout(const void* src_user_va, void* dst_host, std::size_t len);

    void serialize(std::vector<std::uint8_t>& dst) const;
    static std::unique_ptr<UserAddressSpace> deserialize(
        Memory& mem, PhysicalPageManager& ppm, const std::uint8_t*& src, std::size_t& remaining);

private:
    Memory& mem_;
    PhysicalPageManager& ppm_;
    std::uint64_t root_l2_table_pa_{0};
    std::string name_;
    VirtualPageTable vpt_;
    XnuVmMap vm_map_;
    std::vector<std::uint64_t> allocated_l3_tables_{};
    std::vector<std::uint64_t> allocated_physical_pages_{};
};

} // namespace winios::memory
