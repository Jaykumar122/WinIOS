#include "loader/elf_loader.h"
#include "runtime/guest_runtime.h"
#include "test_harness.h"
#include "../support/elf_fixture.h"

#include <limits>
#include <string>

namespace {
using elf_test::Segment;
using elf_test::image;
using elf_test::put;

RuntimeConfig config() {
    return RuntimeConfig{0x8000, 0, 0, 0x6000, 0x1000, 20, std::nullopt, false};
}
void reject_without_writes(const std::vector<std::uint8_t>& bytes) {
    elf_test::File file;
    file.write(bytes);
    Memory memory(0x8000);
    for (std::size_t i = 0; i < memory.size(); ++i) memory.write_u8(i, 0xA5);
    CHECK_INVALID_ARGUMENT(ElfLoader::load(file.path(), memory));
    bool unchanged = true;
    for (std::size_t i = 0; i < memory.size(); ++i) {
        unchanged = unchanged && memory.read_u8(i) == 0xA5;
    }
    CHECK(unchanged);
}
void malformed_headers_and_segments() {
    for (const auto type : {2u, 3u, 7u}) {
        Segment unsupported;
        unsupported.type = type;
        reject_without_writes(image({Segment{}, unsupported}));
    }
    for (const auto offset : {0u, 4u, 5u, 6u, 16u, 18u, 20u, 52u, 54u}) {
        auto bytes = image();
        bytes[offset] ^= 0xFF;
        reject_without_writes(bytes);
    }
    for (const auto count : {0u, 0xFFFFu}) {
        auto bytes = image(); put(bytes, 56, count, 2); reject_without_writes(bytes);
    }
    for (const auto offset : {0ull, 63ull, 0xFFFFFFFFFFFFFFFFull}) {
        auto bytes = image(); put(bytes, 32, offset, 8); reject_without_writes(bytes);
    }
    auto bytes = image(); bytes.resize(100); reject_without_writes(bytes);
    bytes = image(); put(bytes, 64 + 8, bytes.size() + 1, 8); reject_without_writes(bytes);
    bytes = image(); put(bytes, 64 + 32, 0x100, 8); reject_without_writes(bytes);
    bytes = image(); put(bytes, 64 + 40, 3, 8); reject_without_writes(bytes);
    bytes = image(); put(bytes, 64 + 48, 3, 8); reject_without_writes(bytes);
    bytes = image(); put(bytes, 64 + 48, 0x1000, 8); reject_without_writes(bytes);
    bytes = image(); put(bytes, 64 + 16, 0xFFFFFFFFFFFFFFFCull, 8);
    put(bytes, 64 + 40, 8, 8); reject_without_writes(bytes);
    Segment overlapping; overlapping.address = 0x1020;
    reject_without_writes(image({Segment{}, overlapping}));
    Segment nonexecutable; nonexecutable.flags = 6;
    reject_without_writes(image({nonexecutable}));
    Segment empty; empty.size = 0; empty.words.clear();
    reject_without_writes(image({empty}));
    for (std::uint64_t size = 1; size < 4; ++size) {
        Segment partial; partial.words.clear(); partial.size = size;
        reject_without_writes(image({partial}));
    }
    Segment truncated_entry; truncated_entry.size = 6;
    reject_without_writes(image({truncated_entry}, 0x1004));
}
void allocation_limit_and_multisegment_bss() {
    elf_test::File file;
    const auto bytes = image(); file.write(bytes);
    Memory memory(0x8000); memory.write_u8(0x1000, 0xAB);
    test::expect_throw<std::length_error>([&] {
        ElfLoader::load(file.path(), memory, bytes.size() - 1);
    }, "file allocation cap", __FILE__, __LINE__);
    CHECK(memory.read_u8(0x1000) == 0xAB);
    CHECK(ElfLoader::load(file.path(), memory, bytes.size()).entry_address == 0x1000);
    CHECK_RUNTIME_ERROR(ElfLoader::load(file.path().parent_path() / "absent", memory));

    Segment data; data.address = 0x2000; data.flags = 6; data.words = {0x12345678};
    Segment bss; bss.address = 0x3000; bss.flags = 6; bss.words.clear(); bss.size = 0x20;
    file.write(image({Segment{}, data, bss}));
    for (std::size_t i = 0; i < memory.size(); ++i) memory.write_u8(i, 0xA5);
    const auto loaded = ElfLoader::load(file.path(), memory);
    CHECK(loaded.segments.size() == 3);
    CHECK(loaded.loaded_bytes == 0xA0);
    CHECK(memory.read_u32(0x2000) == 0x12345678);
    CHECK(memory.read_u8(0x2004) == 0);
    CHECK(memory.read_u8(0x301F) == 0);
    CHECK(memory.read_u8(0x3020) == 0xA5);
    CHECK(memory.read_u8(0x1800) == 0xA5);
}
void runtime_stack_intervals_and_invalid_configuration() {
    elf_test::File file;
    for (const auto address : {0x5FF0ull, 0x6000ull, 0x6010ull}) {
        Segment data; data.address = address; data.flags = 6;
        file.write(image({Segment{}, data}));
        CHECK(GuestRuntime::execute_elf(file.path(), config()).status
              == ExecutionStatus::invalid_configuration);
    }
    Segment before; before.address = 0x5FC0; before.flags = 6;
    Segment after; after.address = 0x7000; after.flags = 6;
    file.write(image({Segment{}, before, after}));
    CHECK(GuestRuntime::execute_elf(file.path(), config()).status == ExecutionStatus::completed);
    Segment high_code; high_code.address = 0x7000;
    file.write(image({high_code}, 0x7000));
    CHECK(GuestRuntime::execute_elf(file.path(), config()).status == ExecutionStatus::completed);
    file.write(image());
    auto cfg = config(); cfg.stack_size -= 1;
    CHECK(GuestRuntime::execute_elf(file.path(), cfg).status == ExecutionStatus::invalid_configuration);
    cfg = config(); cfg.stack_base = std::numeric_limits<std::uint64_t>::max();
    CHECK(GuestRuntime::execute_elf(file.path(), cfg).status == ExecutionStatus::invalid_configuration);
    cfg = config(); cfg.max_instructions = 0;
    CHECK(GuestRuntime::execute_elf(file.path(), cfg).status == ExecutionStatus::invalid_configuration);
    cfg = config(); cfg.completion_sentinel = 0x4000;
    CHECK(GuestRuntime::execute_elf(file.path(), cfg).status == ExecutionStatus::invalid_configuration);
    cfg = config(); cfg.tracing = true;
    CHECK(GuestRuntime::execute_elf(file.path(), cfg).status == ExecutionStatus::invalid_configuration);
    CHECK(GuestRuntime::execute_elf(file.path().parent_path() / "absent", config()).status
          == ExecutionStatus::load_error);
    file.write({0, 1});
    CHECK(GuestRuntime::execute_elf(file.path(), config()).status == ExecutionStatus::load_error);
}
void traced_execution_is_deterministic() {
    elf_test::File file;
    Segment code;
    code.words = {0xD2800540u, 0xD2802001u, 0xF9000020u, 0xD4400000u};
    file.write(image({code}));
    auto cfg = config(); cfg.tracing = true;
    std::vector<std::string> first_trace, second_trace;
    const auto serialize = [](const CPU::TraceRecord& record) {
        std::string text = std::to_string(record.instruction_address) + ":"
            + std::to_string(record.instruction_word) + ":" + record.disassembly
            + ":" + std::to_string(record.program_counter_after) + ":"
            + std::to_string(record.nzcv_after);
        for (const auto& change : record.changed_registers) text += ":" + change;
        return text;
    };
    const auto first = GuestRuntime::execute_elf(file.path(), cfg,
        [&](const CPU::TraceRecord& record) { first_trace.push_back(serialize(record)); });
    const auto second = GuestRuntime::execute_elf(file.path(), cfg,
        [&](const CPU::TraceRecord& record) { second_trace.push_back(serialize(record)); });
    CHECK(first.status == ExecutionStatus::completed);
    CHECK(first.instructions_executed == 4);
    CHECK(first.memory_at_0x100 == 42);
    CHECK(first.final_stack_pointer == 0x7000);
    CHECK(first.final_registers == second.final_registers);
    CHECK(first.final_pc == second.final_pc);
    CHECK(first_trace == second_trace);
    CHECK(!first_trace.empty());
}
}
int main(int argc, char** argv) {
    const std::string suite = argc > 1 ? argv[1] : "all";
    if (suite != "runtime") {
        malformed_headers_and_segments();
        allocation_limit_and_multisegment_bss();
    }
    if (suite != "loader") {
        runtime_stack_intervals_and_invalid_configuration();
        traced_execution_is_deterministic();
    }
    return test::summarize("stage48_elf_hardening_tests");
}
