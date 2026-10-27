#include "cpu/cpu.h"
#include "test_harness.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

void constructor_starts_zeroed() {
    CPU cpu;

    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        CHECK(cpu.read_register(i) == 0);
    }
    CHECK(cpu.get_program_counter() == 0);
}

void reset_clears_all_registers() {
    CPU cpu;
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        cpu.write_register(i, static_cast<std::uint64_t>(i) + 100);
    }

    cpu.reset();

    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        CHECK(cpu.read_register(i) == 0);
    }
}

void reset_clears_program_counter() {
    CPU cpu;
    cpu.set_program_counter(0xFFFF0000ull);

    cpu.reset();

    CHECK(cpu.get_program_counter() == 0);
}

void register_read_write() {
    CPU cpu;

    cpu.write_register(0, 0x0123456789ABCDEFull);
    cpu.write_register(7, 0xDEADBEEFull);
    cpu.write_register(30, 42);

    CHECK(cpu.read_register(0) == 0x0123456789ABCDEFull);
    CHECK(cpu.read_register(7) == 0xDEADBEEFull);
    CHECK(cpu.read_register(30) == 42);
    CHECK(cpu.read_register(1) == 0);
}

void register_30_is_valid() {
    CPU cpu;

    cpu.write_register(30, std::numeric_limits<std::uint64_t>::max());

    CHECK(cpu.read_register(30) == std::numeric_limits<std::uint64_t>::max());
    CHECK(CPU::kRegisterCount == 31);
}

void register_31_is_rejected() {
    CPU cpu;

    CHECK_OUT_OF_RANGE(cpu.read_register(31));
    CHECK_OUT_OF_RANGE(cpu.write_register(31, 1));
}

void huge_register_index_is_rejected() {
    CPU cpu;
    constexpr std::size_t huge = std::numeric_limits<std::size_t>::max();

    CHECK_OUT_OF_RANGE(cpu.read_register(huge));
    CHECK_OUT_OF_RANGE(cpu.write_register(huge, 1));
}

void program_counter_read_write() {
    CPU cpu;

    cpu.set_program_counter(0x40000000ull);
    CHECK(cpu.get_program_counter() == 0x40000000ull);

    cpu.set_program_counter(0);
    CHECK(cpu.get_program_counter() == 0);

    cpu.set_program_counter(std::numeric_limits<std::uint64_t>::max());
    CHECK(cpu.get_program_counter() == std::numeric_limits<std::uint64_t>::max());
}

void demo_preserves_expected_result() {
    CPU cpu;

    cpu.run_demo();

    CHECK(cpu.read_register(0) == 10);
    CHECK(cpu.read_register(1) == 20);
    CHECK(cpu.read_register(2) == 30);
    CHECK(cpu.get_program_counter() == 0);
}

} // namespace

int main() {
    constructor_starts_zeroed();
    reset_clears_all_registers();
    reset_clears_program_counter();
    register_read_write();
    register_30_is_valid();
    register_31_is_rejected();
    huge_register_index_is_rejected();
    program_counter_read_write();
    demo_preserves_expected_result();

    return test::summarize("cpu_tests");
}
