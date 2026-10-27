#include "runtime/guest_runtime.h"
#include "test_harness.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
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

void invalid_entry_and_sentinel_ranges_are_rejected() {
    TemporaryFile file("ranges");
    write_words(file.path(), {0xD2800540u});

    RuntimeConfig config = valid_config();
    config.entry_address = 2;
    ExecutionResult result = GuestRuntime::execute(file.path(), config);
    CHECK(result.status == ExecutionStatus::invalid_configuration);

    config = valid_config();
    config.entry_address = 4;
    result = GuestRuntime::execute(file.path(), config);
    CHECK(result.status == ExecutionStatus::invalid_configuration);

    config = valid_config();
    config.completion_sentinel = 2;
    result = GuestRuntime::execute(file.path(), config);
    CHECK(result.status == ExecutionStatus::invalid_configuration);

    config = valid_config();
    config.completion_sentinel = 256;
    result = GuestRuntime::execute(file.path(), config);
    CHECK(result.status == ExecutionStatus::invalid_configuration);

    config = valid_config();
    config.load_address = std::numeric_limits<std::uint64_t>::max();
    result = GuestRuntime::execute(file.path(), config);
    CHECK(result.status == ExecutionStatus::invalid_configuration);

    config = valid_config();
    config.stack_base = std::numeric_limits<std::uint64_t>::max();
    result = GuestRuntime::execute(file.path(), config);
    CHECK(result.status == ExecutionStatus::invalid_configuration);
}

void zero_limit_is_invalid_and_exact_limit_is_reported() {
    TemporaryFile file("limit_boundary");
    write_words(file.path(), {0xD2800540u});
    RuntimeConfig config = valid_config();

    config.max_instructions = 0;
    ExecutionResult result = GuestRuntime::execute(file.path(), config);
    CHECK(result.status == ExecutionStatus::invalid_configuration);

    config.max_instructions = 1;
    result = GuestRuntime::execute(file.path(), config);
    CHECK(result.status == ExecutionStatus::instruction_limit_reached);
    CHECK(result.instructions_executed == 1);
    CHECK(result.final_pc == 4);
    CHECK(result.final_registers[0] == 42);
}

void invalid_branch_target_is_a_memory_fault() {
    TemporaryFile file("branch_fault");
    write_words(file.path(), {0x14000200u}); // B +2048, beyond 512-byte RAM
    const ExecutionResult result = GuestRuntime::execute(file.path(), valid_config());

    CHECK(result.status == ExecutionStatus::memory_fault);
    CHECK(result.instructions_executed == 1);
    CHECK(result.final_pc == 2048);
    CHECK(!result.error.empty());
}

void repeated_execution_does_not_reuse_cpu_state() {
    TemporaryFile file("repeated");
    write_words(file.path(), {0xD2800540u}); // MOVZ X0, #42
    RuntimeConfig config = valid_config();
    config.max_instructions = 1;

    ExecutionResult first = GuestRuntime::execute(file.path(), config);
    CHECK(first.status == ExecutionStatus::instruction_limit_reached);
    CHECK(first.final_registers[0] == 42);
    CHECK(first.instructions_executed == 1);

    write_words(file.path(), {0xD28000E0u}); // MOVZ X0, #7
    ExecutionResult second = GuestRuntime::execute(file.path(), config);
    CHECK(second.status == ExecutionStatus::instruction_limit_reached);
    CHECK(second.final_registers[0] == 7);
    CHECK(second.instructions_executed == 1);
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
    invalid_entry_and_sentinel_ranges_are_rejected();
    zero_limit_is_invalid_and_exact_limit_is_reported();
    invalid_branch_target_is_a_memory_fault();
    repeated_execution_does_not_reuse_cpu_state();
    unsupported_and_memory_errors_are_distinct();
    successful_program_reports_register_and_memory_state_via_trace();
    return test::summarize("runtime_tests");
}
