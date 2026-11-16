#include "device/mmio_diagnostics.h"

#include <iomanip>
#include <sstream>

namespace winios::device {

const char* device_classification_name(DeviceClassification cls) noexcept {
    switch (cls) {
    case DeviceClassification::Dram: return "DRAM";
    case DeviceClassification::AppleAic: return "AppleAIC2";
    case DeviceClassification::ArmGic: return "ArmGicV3";
    case DeviceClassification::Uart: return "UART";
    case DeviceClassification::ApplePmgr: return "ApplePMGR";
    case DeviceClassification::AppleAns2: return "AppleANS2";
    case DeviceClassification::AppleDisplay: return "AppleDisplay";
    case DeviceClassification::AppleDart: return "AppleDART";
    case DeviceClassification::UnmappedFault: return "UnmappedFault";
    default: return "Unknown";
    }
}

MmioDiagnostics& MmioDiagnostics::instance() noexcept {
    static MmioDiagnostics diag;
    return diag;
}

void MmioDiagnostics::record(const MmioTransaction& tx) {
    std::lock_guard<std::mutex> lock(mutex_);
    history_.push_back(tx);
    if (tx.faulted) {
        ++fault_count_;
    }
}

void MmioDiagnostics::record_fault(std::uint64_t pc, std::uint64_t address, unsigned width,
                                   bool is_write, unsigned el, const std::string& reason) {
    MmioTransaction tx{};
    tx.pc = pc;
    tx.physical_address = address;
    tx.width = width;
    tx.is_write = is_write;
    tx.privilege_el = el;
    tx.device_class = classify_address(address);
    tx.faulted = true;
    tx.details = reason;
    record(tx);
}

std::vector<MmioTransaction> MmioDiagnostics::get_transactions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return history_;
}

std::size_t MmioDiagnostics::transaction_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return history_.size();
}

std::size_t MmioDiagnostics::fault_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return fault_count_;
}

void MmioDiagnostics::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    history_.clear();
    fault_count_ = 0;
}

DeviceClassification MmioDiagnostics::classify_address(std::uint64_t address) noexcept {
    // Apple Silicon A16 Bionic (T8120) memory map heuristics
    if (address >= 0x23B100000ULL && address < 0x23B200000ULL) {
        return DeviceClassification::AppleAic;
    }
    if (address >= 0x08000000ULL && address < 0x08100000ULL) {
        return DeviceClassification::ArmGic;
    }
    if (address == 0x90000ULL || (address >= 0x235200000ULL && address < 0x235204000ULL)) {
        return DeviceClassification::Uart;
    }
    if (address >= 0x23B000000ULL && address < 0x23B080000ULL) {
        return DeviceClassification::ApplePmgr;
    }
    if (address >= 0x27C000000ULL && address < 0x27C080000ULL) {
        return DeviceClassification::AppleAns2;
    }
    if (address >= 0x280000000ULL && address < 0x280080000ULL) {
        return DeviceClassification::AppleDart;
    }
    if (address < 0x80000000ULL) {
        return DeviceClassification::Dram;
    }
    return DeviceClassification::UnmappedFault;
}

std::string MmioDiagnostics::to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"total_transactions\": " << history_.size() << ",\n";
    ss << "  \"total_faults\": " << fault_count_ << ",\n";
    ss << "  \"transactions\": [\n";

    for (std::size_t i = 0; i < history_.size(); ++i) {
        const auto& tx = history_[i];
        ss << "    {\n";
        ss << "      \"cycle\": " << tx.cycle << ",\n";
        ss << "      \"pc\": \"0x" << std::hex << tx.pc << std::dec << "\",\n";
        ss << "      \"address\": \"0x" << std::hex << tx.physical_address << std::dec << "\",\n";
        ss << "      \"width\": " << tx.width << ",\n";
        ss << "      \"direction\": \"" << (tx.is_write ? "WRITE" : "READ") << "\",\n";
        ss << "      \"value\": \"0x" << std::hex << tx.value << std::dec << "\",\n";
        ss << "      \"privilege\": \"EL" << tx.privilege_el << "\",\n";
        ss << "      \"classification\": \"" << device_classification_name(tx.device_class) << "\",\n";
        ss << "      \"faulted\": " << (tx.faulted ? "true" : "false") << ",\n";
        ss << "      \"details\": \"" << tx.details << "\"\n";
        ss << "    }" << (i + 1 < history_.size() ? "," : "") << "\n";
    }

    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

std::string MmioDiagnostics::to_markdown_report() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << "# MMIO Transaction Diagnostics Report\n\n";
    ss << "- **Total Logged Transactions:** " << history_.size() << "\n";
    ss << "- **Total Trapped Faults:** " << fault_count_ << "\n\n";
    ss << "| Index | Cycle | PC | Physical Address | Width | Dir | Value | Privilege | Device | Fault |\n";
    ss << "|---|---|---|---|---|---|---|---|---|---|\n";

    for (std::size_t i = 0; i < history_.size(); ++i) {
        const auto& tx = history_[i];
        ss << "| " << i
           << " | " << tx.cycle
           << " | 0x" << std::hex << tx.pc << std::dec
           << " | 0x" << std::hex << tx.physical_address << std::dec
           << " | " << tx.width
           << " | " << (tx.is_write ? "WR" : "RD")
           << " | 0x" << std::hex << tx.value << std::dec
           << " | EL" << tx.privilege_el
           << " | " << device_classification_name(tx.device_class)
           << " | " << (tx.faulted ? "YES" : "NO")
           << " |\n";
    }

    return ss.str();
}

} // namespace winios::device
