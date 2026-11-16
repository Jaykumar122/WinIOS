#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "firmware/apple_boot_args.h"

class Memory;

namespace winios::firmware {

class KernelBootArgsBuilder {
public:
    KernelBootArgsBuilder() noexcept;

    KernelBootArgsBuilder& set_virt_base(std::uint64_t addr) noexcept;
    KernelBootArgsBuilder& set_phys_base(std::uint64_t addr) noexcept;
    KernelBootArgsBuilder& set_mem_size(std::uint64_t size) noexcept;
    KernelBootArgsBuilder& set_top_of_kernel_data(std::uint64_t addr) noexcept;
    KernelBootArgsBuilder& set_device_tree(std::uint64_t phys_addr, std::uint32_t length) noexcept;
    KernelBootArgsBuilder& set_command_line(const std::string& cmdline) noexcept;
    KernelBootArgsBuilder& set_boot_flags(std::uint64_t flags) noexcept;
    KernelBootArgsBuilder& set_machine_type(std::uint32_t machine_type) noexcept;
    KernelBootArgsBuilder& set_video(
        std::uint64_t base_addr,
        std::uint64_t display,
        std::uint64_t row_bytes,
        std::uint64_t width,
        std::uint64_t height,
        std::uint64_t depth) noexcept;
    KernelBootArgsBuilder& set_video(const AppleBootVideo& video) noexcept;

    // Produces the populated AppleBootArgsRev2 struct
    AppleBootArgsRev2 build() const noexcept;

    // Serializes the struct to exactly 736 bytes
    std::vector<std::uint8_t> serialize() const;

    // Validates the boot arguments against Apple XNU contract rules
    bool validate(std::string* out_error = nullptr) const;

    // Writes the serialized boot arguments to target physical address in guest RAM
    bool write_to_memory(
        ::Memory& memory,
        std::uint64_t target_pa,
        std::string* out_error = nullptr) const;

    const AppleBootArgsRev2& raw_args() const noexcept { return args_; }

private:
    AppleBootArgsRev2 args_{};
};

} // namespace winios::firmware
