#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace winios::loader {

struct InstructionProvenanceEntry {
    std::uint64_t seq_num{0};
    std::uint64_t pc{0};
    std::uint32_t opcode{0};
    std::string mnemonic;
    std::string binary_sha256;
    std::string segment;
    std::string symbol;
    std::string source_file;
    std::string source_revision;
    std::string category;
};

class XnuInstructionProvenanceTracker {
public:
    static XnuInstructionProvenanceTracker& instance();

    void reset() noexcept;

    void set_binary_sha256(const std::string& sha256) {
        binary_sha256_ = sha256;
    }

    void record_instruction(std::uint64_t pc, std::uint32_t opcode, const std::string& mnemonic = "");

    std::size_t count() const noexcept { return entries_.size(); }
    const std::vector<InstructionProvenanceEntry>& entries() const noexcept { return entries_; }

    std::string export_json() const;

private:
    XnuInstructionProvenanceTracker() = default;

    std::string map_pc_to_symbol(std::uint64_t pc) const;
    std::string map_pc_to_source_file(std::uint64_t pc) const;
    std::string map_pc_to_category(std::uint64_t pc) const;

    std::string binary_sha256_;
    std::vector<InstructionProvenanceEntry> entries_;
    mutable std::mutex mutex_;
};

} // namespace winios::loader
