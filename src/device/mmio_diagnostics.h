#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace winios::device {

enum class DeviceClassification : std::uint8_t {
    Dram = 0,
    AppleAic,
    ArmGic,
    Uart,
    ApplePmgr,
    AppleAns2,
    AppleDisplay,
    AppleDart,
    UnmappedFault
};

[[nodiscard]] const char* device_classification_name(DeviceClassification cls) noexcept;

struct MmioTransaction {
    std::uint64_t cycle{0};
    std::uint64_t pc{0};
    std::uint64_t physical_address{0};
    unsigned width{0};
    bool is_write{false};
    std::uint64_t value{0};
    unsigned privilege_el{0};
    DeviceClassification device_class{DeviceClassification::UnmappedFault};
    bool faulted{false};
    std::string details;
};

class MmioDiagnostics {
public:
    static MmioDiagnostics& instance() noexcept;

    void record(const MmioTransaction& tx);
    void record_fault(std::uint64_t pc, std::uint64_t address, unsigned width, bool is_write,
                      unsigned el, const std::string& reason);

    [[nodiscard]] std::vector<MmioTransaction> get_transactions() const;
    [[nodiscard]] std::size_t transaction_count() const noexcept;
    [[nodiscard]] std::size_t fault_count() const noexcept;
    void clear() noexcept;

    [[nodiscard]] static DeviceClassification classify_address(std::uint64_t address) noexcept;
    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] std::string to_markdown_report() const;

private:
    MmioDiagnostics() = default;
    mutable std::mutex mutex_;
    std::vector<MmioTransaction> history_;
    std::size_t fault_count_{0};
};

} // namespace winios::device
