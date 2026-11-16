#include "loader/xnu_instruction_provenance_tracker.h"
#include <sstream>
#include <iomanip>

namespace winios::loader {

XnuInstructionProvenanceTracker& XnuInstructionProvenanceTracker::instance() {
    static XnuInstructionProvenanceTracker tracker;
    return tracker;
}

void XnuInstructionProvenanceTracker::reset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    binary_sha256_.clear();
}

std::string XnuInstructionProvenanceTracker::map_pc_to_symbol(std::uint64_t pc) const {
    if (pc >= 0x1BA0 && pc <= 0x1BD0) return "_start";
    if (pc >= 0x1B8C && pc < 0x1BA0) return "pe_arm_init";
    if (pc >= 0x19B4 && pc < 0x1B8C) return "PE_init_platform";
    if (pc >= 0x1480 && pc < 0x14DC) return "arm_vm_init_early";
    if (pc >= 0x14DC && pc < 0x19B4) return "arm_init";
    if (pc >= 0x1BAC && pc <= 0x1BB4) return "secondary_park";
    return "unknown_kernel_code";
}

std::string XnuInstructionProvenanceTracker::map_pc_to_source_file(std::uint64_t pc) const {
    if (pc >= 0x1BA0 && pc <= 0x1BD0) return "src/kernel/xnu_start.s (osfmk/arm64/start.s)";
    if (pc >= 0x1B8C && pc < 0x1BA0) return "src/kernel/xnu_pe_init.c (pexpert/arm/pe_bootargs.c)";
    if (pc >= 0x19B4 && pc < 0x1B8C) return "src/kernel/xnu_pe_init.c (pexpert/arm/pe_init.c)";
    if (pc >= 0x1480 && pc < 0x14DC) return "src/kernel/xnu_arm_init.c (osfmk/arm64/arm_vm_init.c)";
    if (pc >= 0x14DC && pc < 0x19B4) return "src/kernel/xnu_arm_init.c (osfmk/arm64/arm_init.c)";
    return "src/kernel/xnu_start.s";
}

std::string XnuInstructionProvenanceTracker::map_pc_to_category(std::uint64_t pc) const {
    if (pc >= 0x1B8C && pc < 0x1BA0) return "Category A/B (Direct Source-Derived)";
    if (pc >= 0x1BA0 && pc <= 0x1BD0) return "Category B (Adapted Assembly)";
    if (pc >= 0x1480 && pc < 0x14DC) return "Category B/C (Adapted Page Stealer)";
    if (pc >= 0x14DC && pc < 0x1B8C) return "Category B (Adapted C Runtime)";
    return "Category C (Compatibility Code)";
}

void XnuInstructionProvenanceTracker::record_instruction(
    std::uint64_t pc,
    std::uint32_t opcode,
    const std::string& mnemonic) {

    std::lock_guard<std::mutex> lock(mutex_);

    InstructionProvenanceEntry entry;
    entry.seq_num = entries_.size() + 1;
    entry.pc = pc;
    entry.opcode = opcode;
    entry.mnemonic = mnemonic.empty() ? "arm64_instruction" : mnemonic;
    entry.binary_sha256 = binary_sha256_;
    entry.segment = "__TEXT";
    entry.symbol = map_pc_to_symbol(pc);
    entry.source_file = map_pc_to_source_file(pc);
    entry.source_revision = "xnu-10002.1.13";
    entry.category = map_pc_to_category(pc);

    entries_.push_back(entry);
}

std::string XnuInstructionProvenanceTracker::export_json() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"campaign\": 208,\n";
    ss << "  \"total_retired\": " << entries_.size() << ",\n";
    ss << "  \"binary_sha256\": \"" << binary_sha256_ << "\",\n";
    ss << "  \"source_revision\": \"xnu-10002.1.13\",\n";
    ss << "  \"instructions\": [\n";

    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        ss << "    {\n";
        ss << "      \"seq\": " << e.seq_num << ",\n";
        ss << "      \"pc\": \"0x" << std::hex << e.pc << std::dec << "\",\n";
        ss << "      \"opcode\": \"0x" << std::hex << std::setw(8) << std::setfill('0') << e.opcode << std::dec << "\",\n";
        ss << "      \"symbol\": \"" << e.symbol << "\",\n";
        ss << "      \"source_file\": \"" << e.source_file << "\",\n";
        ss << "      \"category\": \"" << e.category << "\"\n";
        ss << "    }" << (i + 1 < entries_.size() ? "," : "") << "\n";
    }

    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

} // namespace winios::loader
