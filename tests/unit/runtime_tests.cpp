#include "runtime/guest_runtime.h"
#include "test_harness.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
class TemporaryFile {
public:
    explicit TemporaryFile(const char* name)
        : path_(std::filesystem::temp_directory_path()
                / (std::string("winios_runtime_") + name + ".bin")) {}
    ~TemporaryFile() { std::error_code ignored; std::filesystem::remove(path_, ignored); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write_words(const std::filesystem::path& path,
                 const std::vector<std::uint32_t>& words) {
    std::ofstream output(path, std::ios::binary);
    for (const std::uint32_t word : words) {
        output.put(static_cast<char>(word & 0xFFu));
        output.put(static_cast<char>((word >> 8u) & 0xFFu));
        output.put(static_cast<char>((word >> 16u) & 0xFFu));
        output.put(static_cast<char>((word >> 24u) & 0xFFu));
    }
}

RuntimeConfig valid_config() {
    return RuntimeConfig{512, 0, 0, 256, 128, 10, std::nullopt, false};
}

void completion_policy_reports_success() {
    TemporaryFile file("complete");
    write_words(file.path(), {0xD2800540u, 0x14000001u});
    RuntimeConfig config = valid_config();
    config.completion_sentinel = 8;

    const ExecutionResult result = GuestRuntime::execute(file.path(), config);

    CHECK(result.status == ExecutionStatus::completed);
    CHECK(result.instructions_executed == 2);
    CHECK(result.final_pc == 8);
}

void limit_exhaustion_is_distinct() {
    TemporaryFile file("limit");
    write_words(file.path(), {0x14000000u}); // B .
    RuntimeConfig config = valid_config();
    config.max_instructions = 3;

    const ExecutionResult result = GuestRuntime::execute(file.path(), config);

    CHECK(result.status == ExecutionStatus::instruction_limit_reached);
    CHECK(result.instructions_executed == 3);
    CHECK(result.final_pc == 0);
}

void invalid_configuration_is_rejected() {
    TemporaryFile file("config");
    write_words(file.path(), {0xD2800540u});
    RuntimeConfig config = valid_config();
    config.stack_base = 64;
    config.stack_size = 512;

    const ExecutionResult result = GuestRuntime::execute(file.path(), config);

    CHECK(result.status == ExecutionStatus::invalid_configuration);
    CHECK(result.instructions_executed == 0);
}

void unsupported_and_memory_errors_are_distinct() {
    TemporaryFile unsupported("unsupported");
    write_words(unsupported.path(), {0});
    RuntimeConfig config = valid_config();
    const ExecutionResult unsupported_result =
        GuestRuntime::execute(unsupported.path(), config);
    CHECK(unsupported_result.status == ExecutionStatus::unsupported_instruction);
    CHECK(unsupported_result.instructions_executed == 0);

    TemporaryFile memory_fault("memory");
    write_words(memory_fault.path(), {0xD2802001u, 0xF9400020u});
    config.memory_size = 64;
    config.stack_base = 32;
    config.stack_size = 16;
    const ExecutionResult memory_result =
        GuestRuntime::execute(memory_fault.path(), config);
    CHECK(memory_result.status == ExecutionStatus::memory_fault);
    CHECK(memory_result.instructions_executed == 1);
    CHECK(memory_result.final_pc == 4);
}

void successful_program_reports_register_and_memory_state_via_trace() {
    TemporaryFile file("program");
    write_words(file.path(), {
        0xD2800540u, // MOVZ X0, #42
        0x91001400u, // ADD X0, X0, #5
        0xD2802001u, // MOVZ X1, #0x100
        0xF9000020u, // STR X0, [X1, #0]
        0x14000002u  // B +8, completion sentinel
    });
    RuntimeConfig config{512, 0, 0, 384, 128, 10, 24, true};
    std::size_t traces = 0;
    const ExecutionResult result = GuestRuntime::execute(
        file.path(), config, [&](const CPU::TraceRecord&) { ++traces; });

    CHECK(result.status == ExecutionStatus::completed);
    CHECK(result.instructions_executed == 5);
    CHECK(result.final_pc == 24);
    CHECK(result.loaded_bytes == 20);
    CHECK(traces == 5);
    CHECK(result.memory_at_0x100.has_value());
    CHECK(*result.memory_at_0x100 == 47);
}
} // namespace

int main() {
    completion_policy_reports_success();
    limit_exhaustion_is_distinct();
    invalid_configuration_is_rejected();
    unsupported_and_memory_errors_are_distinct();
    successful_program_reports_register_and_memory_state_via_trace();
    return test::summarize("runtime_tests");
}
