#pragma once

#include <cstddef>
#include <cstdint>

namespace winios::cpu {

enum class InterruptControllerKind : std::uint8_t {
    None = 0,
    GicV3,
    AppleAic2,
    Custom
};

class IInterruptController {
public:
    virtual ~IInterruptController() = default;

    [[nodiscard]] virtual InterruptControllerKind kind() const noexcept = 0;
    [[nodiscard]] virtual bool is_mmio_address(std::uint64_t pa) const noexcept = 0;
    virtual std::uint64_t read_mmio(std::uint64_t pa, unsigned width, std::uint32_t core_id) = 0;
    virtual void write_mmio(std::uint64_t pa, std::uint64_t value, unsigned width, std::uint32_t core_id) = 0;
    [[nodiscard]] virtual bool has_pending_interrupt(std::size_t core_id) const noexcept = 0;
    virtual void reset() noexcept = 0;
};

} // namespace winios::cpu
