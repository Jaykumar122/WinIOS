#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::loader {

struct IndependentKernelFixtureConfig {
    std::uint64_t text_vmaddr{0x0000000000000000ULL};
    std::uint64_t entry_vaddr{0x0000000000001000ULL};
    std::uint64_t el0_code_vaddr{0x0000000000002000ULL};
    std::uint64_t vbar_vaddr{0x0000000000002800ULL}; // 2048-byte aligned
    std::uint64_t status_target_addr{0x0000000000003000ULL};
    std::uint64_t status_magic{0xCAFEFEEDC001CAFEULL};
};

class IndependentKernelFixtureBuilder {
public:
    // Builds a standalone ARM64 Mach-O kernel fixture
    static std::vector<std::uint8_t> build_fixture(const IndependentKernelFixtureConfig& config);
};

} // namespace winios::loader
