#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::loader {

struct XnuContinuationConfig {
    std::uint64_t text_vmaddr{0x0ULL};
    std::uint64_t entry_vaddr{0x1000ULL};
    std::uint64_t vbar_vaddr{0x4000ULL};
    std::uint64_t aic_base{0x23B100000ULL};
    std::uint64_t uart_base{0x90000ULL};
    std::uint64_t status_mailbox_pa{0x20000ULL + 736ULL + 0x100ULL};
};

class XnuBootstrapContinuationBuilder {
public:
    static std::vector<std::uint8_t> build_image(const XnuContinuationConfig& config = {});
    static std::string compute_sha256(const std::vector<std::uint8_t>& image);
};

} // namespace winios::loader
