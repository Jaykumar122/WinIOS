#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::loader {

struct XnuGenuineTraceRecord {
    std::uint64_t pc{0};
    std::uint32_t raw_instruction{0};
    std::string   mnemonic;
    std::string   symbol;
    std::string   provenance;
    bool          executed{false};
};

struct XnuGenuineExecutionResult {
    bool          success{false};
    std::size_t   total_instructions_retired{0};
    std::size_t   start_instructions_retired{0};
    std::size_t   c_instructions_retired{0};
    std::size_t   pe_instructions_retired{0};
    std::uint64_t entry_pc{0};
    std::uint64_t arm_init_pc{0};
    std::uint64_t pe_init_pc{0};
    std::uint64_t mailbox_value{0};
    std::string   uart_output;
    std::uint32_t aic_info_captured{0};
    std::uint32_t aic_whoami_captured{0};
    bool          reached_wfi{false};
    std::string   image_sha256;
    std::vector<XnuGenuineTraceRecord> trace;
    std::string   error_message;
};

class XnuGenuineInitBuilder {
public:
    // Returns the embedded bit-exact genuine compiled Mach-O image (Campaign 207)
    static std::vector<std::uint8_t> get_embedded_image();

    // Returns the embedded bit-exact genuine compiled Mach-O image (Campaign 208)
    static std::vector<std::uint8_t> get_embedded_image_208();

    // Returns the embedded bit-exact genuine compiled Mach-O image (Campaign 209)
    static std::vector<std::uint8_t> get_embedded_image_209();

    // Returns the embedded bit-exact genuine compiled Mach-O image (Campaign 210)
    static std::vector<std::uint8_t> get_embedded_image_210();

    // Loads the genuine image from disk if available, otherwise falls back to embedded
    static std::vector<std::uint8_t> load_or_get_image(const std::string& preferred_path = "");

    // Loads the Campaign 208 image specifically
    static std::vector<std::uint8_t> load_or_get_image_208(const std::string& preferred_path = "");

    // Loads the Campaign 209 image specifically
    static std::vector<std::uint8_t> load_or_get_image_209(const std::string& preferred_path = "");

    // Loads the Campaign 210 image specifically
    static std::vector<std::uint8_t> load_or_get_image_210(const std::string& preferred_path = "");

    // Attempts to recompile directly from src/kernel/xnu_*.c and .s using zig cc
    static bool recompile_with_zig(
        std::vector<std::uint8_t>& out_image,
        std::string* out_error = nullptr);

    // Computes SHA-256 digest of image
    static std::string compute_sha256(const std::vector<std::uint8_t>& image);

    // Disassembles ARM64 instruction word into standard text mnemonic
    static std::string disassemble(std::uint32_t insn, std::uint64_t pc);
};

} // namespace winios::loader
