#include "kernel_boot_args_builder.h"

#include <cstring>
#include <iomanip>
#include <sstream>

#include "memory/memory.h"

namespace winios::firmware {

KernelBootArgsBuilder::KernelBootArgsBuilder() noexcept {
    args_.Revision = kAppleBootArgsRevision2;
    args_.Version = kAppleBootArgsVersion2;
}

KernelBootArgsBuilder& KernelBootArgsBuilder::set_virt_base(std::uint64_t addr) noexcept {
    args_.virtBase = addr;
    return *this;
}

KernelBootArgsBuilder& KernelBootArgsBuilder::set_phys_base(std::uint64_t addr) noexcept {
    args_.physBase = addr;
    return *this;
}

KernelBootArgsBuilder& KernelBootArgsBuilder::set_mem_size(std::uint64_t size) noexcept {
    args_.memSize = size;
    args_.memSizeActual = size;
    return *this;
}

KernelBootArgsBuilder& KernelBootArgsBuilder::set_top_of_kernel_data(std::uint64_t addr) noexcept {
    args_.topOfKernelData = addr;
    return *this;
}

KernelBootArgsBuilder& KernelBootArgsBuilder::set_device_tree(std::uint64_t phys_addr, std::uint32_t length) noexcept {
    args_.deviceTreeP = phys_addr;
    args_.deviceTreeLength = length;
    return *this;
}

KernelBootArgsBuilder& KernelBootArgsBuilder::set_command_line(const std::string& cmdline) noexcept {
    std::memset(args_.CommandLine, 0, sizeof(args_.CommandLine));
    const std::size_t copy_len = (cmdline.size() < sizeof(args_.CommandLine) - 1)
                                     ? cmdline.size()
                                     : sizeof(args_.CommandLine) - 1;
    std::memcpy(args_.CommandLine, cmdline.data(), copy_len);
    args_.CommandLine[copy_len] = '\0';
    return *this;
}

KernelBootArgsBuilder& KernelBootArgsBuilder::set_boot_flags(std::uint64_t flags) noexcept {
    args_.bootFlags = flags;
    return *this;
}

KernelBootArgsBuilder& KernelBootArgsBuilder::set_machine_type(std::uint32_t machine_type) noexcept {
    args_.machineType = machine_type;
    return *this;
}

KernelBootArgsBuilder& KernelBootArgsBuilder::set_video(
    std::uint64_t base_addr,
    std::uint64_t display,
    std::uint64_t row_bytes,
    std::uint64_t width,
    std::uint64_t height,
    std::uint64_t depth) noexcept {

    args_.Video.v_baseAddr = base_addr;
    args_.Video.v_display = display;
    args_.Video.v_rowBytes = row_bytes;
    args_.Video.v_width = width;
    args_.Video.v_height = height;
    args_.Video.v_depth = depth;
    return *this;
}

KernelBootArgsBuilder& KernelBootArgsBuilder::set_video(const AppleBootVideo& video) noexcept {
    args_.Video = video;
    return *this;
}

AppleBootArgsRev2 KernelBootArgsBuilder::build() const noexcept {
    return args_;
}

std::vector<std::uint8_t> KernelBootArgsBuilder::serialize() const {
    std::vector<std::uint8_t> buffer(sizeof(AppleBootArgsRev2));
    std::memcpy(buffer.data(), &args_, sizeof(AppleBootArgsRev2));
    return buffer;
}

bool KernelBootArgsBuilder::validate(std::string* out_error) const {
    if (args_.Revision != kAppleBootArgsRevision2) {
        if (out_error) *out_error = "Invalid Revision: expected 2";
        return false;
    }
    if (args_.Version != kAppleBootArgsVersion2) {
        if (out_error) *out_error = "Invalid Version: expected 2";
        return false;
    }
    if ((args_.physBase % 0x4000) != 0) {
        if (out_error) *out_error = "Physical base address must be 16KB page aligned";
        return false;
    }
    if (args_.memSize == 0) {
        if (out_error) *out_error = "Memory size cannot be zero";
        return false;
    }
    if (args_.topOfKernelData != 0 &&
        (args_.topOfKernelData < args_.physBase || args_.topOfKernelData > args_.physBase + args_.memSize)) {
        if (out_error) *out_error = "topOfKernelData is out of physical memory bounds";
        return false;
    }
    if (args_.deviceTreeP != 0 && args_.deviceTreeLength == 0) {
        if (out_error) *out_error = "deviceTreeP set but deviceTreeLength is zero";
        return false;
    }
    if (args_.deviceTreeLength != 0 && args_.deviceTreeP == 0) {
        if (out_error) *out_error = "deviceTreeLength set but deviceTreeP is zero";
        return false;
    }
    return true;
}

bool KernelBootArgsBuilder::write_to_memory(
    ::Memory& memory,
    std::uint64_t target_pa,
    std::string* out_error) const {

    if ((target_pa % 8) != 0) {
        if (out_error) *out_error = "target_pa must be 8-byte aligned";
        return false;
    }

    if (target_pa + sizeof(AppleBootArgsRev2) > memory.size()) {
        if (out_error) {
            std::ostringstream oss;
            oss << "Boot args at 0x" << std::hex << target_pa
                << " (size 736) exceeds guest memory size 0x" << memory.size();
            *out_error = oss.str();
        }
        return false;
    }

    if (!validate(out_error)) {
        return false;
    }

    const auto bytes = serialize();
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        memory.write_u8(static_cast<std::size_t>(target_pa + i), bytes[i]);
    }
    return true;
}

} // namespace winios::firmware
