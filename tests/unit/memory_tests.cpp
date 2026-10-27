#include "memory/memory.h"
#include "test_harness.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

void zero_initialized() {
    Memory memory(16);

    CHECK(memory.size() == 16);
    for (std::size_t i = 0; i < memory.size(); ++i) {
        CHECK(memory.read_u8(i) == 0);
    }
    CHECK(memory.read_u32(0) == 0);
    CHECK(memory.read_u64(8) == 0);
}

void byte_round_trip() {
    Memory memory(4);

    memory.write_u8(0, std::uint8_t{0x12});
    memory.write_u8(3, std::uint8_t{0xFF});

    CHECK(memory.read_u8(0) == 0x12);
    CHECK(memory.read_u8(3) == 0xFF);
    CHECK(memory.read_u8(1) == 0);
    CHECK(memory.read_u8(2) == 0);
}

void word32_round_trip() {
    Memory memory(8);

    memory.write_u32(4, 0xDEADBEEFu);

    CHECK(memory.read_u32(4) == 0xDEADBEEFu);
    CHECK(memory.read_u32(0) == 0);
    CHECK(memory.read_u8(4) == 0xEF);
}

void word64_round_trip() {
    Memory memory(16);

    memory.write_u64(8, 0x0123456789ABCDEFull);

    CHECK(memory.read_u64(8) == 0x0123456789ABCDEFull);
    CHECK(memory.read_u64(0) == 0);
    CHECK(memory.read_u32(8) == 0x89ABCDEFu);
    CHECK(memory.read_u32(12) == 0x01234567u);
}

void little_endian_layout() {
    Memory memory(8);

    memory.write_u32(0, 0x11223344u);
    CHECK(memory.read_u8(0) == 0x44);
    CHECK(memory.read_u8(1) == 0x33);
    CHECK(memory.read_u8(2) == 0x22);
    CHECK(memory.read_u8(3) == 0x11);

    memory.write_u64(0, 0x0102030405060708ull);
    CHECK(memory.read_u8(0) == 0x08);
    CHECK(memory.read_u8(1) == 0x07);
    CHECK(memory.read_u8(2) == 0x06);
    CHECK(memory.read_u8(3) == 0x05);
    CHECK(memory.read_u8(4) == 0x04);
    CHECK(memory.read_u8(5) == 0x03);
    CHECK(memory.read_u8(6) == 0x02);
    CHECK(memory.read_u8(7) == 0x01);
}

void final_valid_byte() {
    Memory memory(4);

    memory.write_u8(3, std::uint8_t{0xA5});

    CHECK(memory.read_u8(3) == 0xA5);
}

void out_of_bounds_byte() {
    Memory memory(4);

    CHECK_OUT_OF_RANGE(memory.read_u8(4));
    CHECK_OUT_OF_RANGE(memory.write_u8(4, std::uint8_t{0x01}));
}

void huge_offset_rejected() {
    Memory memory(4);
    constexpr std::size_t huge = std::numeric_limits<std::size_t>::max();

    CHECK_OUT_OF_RANGE(memory.read_u8(huge));
    CHECK_OUT_OF_RANGE(memory.read_u32(huge));
    CHECK_OUT_OF_RANGE(memory.write_u64(huge, 0));
}

void multi_byte_access_crossing_boundary() {
    Memory memory(8);

    CHECK_OUT_OF_RANGE(memory.read_u32(6));
    CHECK_OUT_OF_RANGE(memory.write_u32(6, 0x11223344u));
    CHECK_OUT_OF_RANGE(memory.read_u64(1));
    CHECK_OUT_OF_RANGE(memory.write_u64(1, 0x1122334455667788ull));
}

void zero_size_rejected() {
    CHECK_INVALID_ARGUMENT(Memory(0));
}

} // namespace

int main() {
    zero_initialized();
    byte_round_trip();
    word32_round_trip();
    word64_round_trip();
    little_endian_layout();
    final_valid_byte();
    out_of_bounds_byte();
    huge_offset_rejected();
    multi_byte_access_crossing_boundary();
    zero_size_rejected();

    return test::summarize("memory_tests");
}
