#include "trace/trace_serializer.h"
#include "trace/trace_event.h"
#include "trace/trace_types.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using namespace winios::trace;

namespace {

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at line " << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while(0)

void test_1_crc32c_test_vectors() {
    std::cout << "  Test 1: CRC32C standard test vector verification... ";
    // Standard RFC 3720 test vector: ASCII "123456789" (9 bytes) -> 0xE3069283
    const char test_data[] = "123456789";
    const std::uint32_t crc = calculate_crc32c(test_data, 9);
    TEST_ASSERT(crc == 0xE3069283u);

    // Empty buffer test
    TEST_ASSERT(calculate_crc32c(nullptr, 0) == 0);
    std::cout << "PASS (0xE3069283 verified)\n";
}

void test_2_encode_decode_known_event() {
    std::cout << "  Test 2: Encode and decode known event... ";
    TraceEvent ev{};
    ev.sequence_number = 100;
    ev.virtual_time = 200;
    ev.pc = 0x4000;
    ev.address = 0x8000;
    ev.data = 0xCAFEBABE;
    ev.opcode_or_syndrome = 0xD503201F;
    ev.flags = static_cast<std::uint32_t>(TraceEventFlags::HasVirtualAddress | TraceEventFlags::IsWrite);
    ev.event_type = static_cast<std::uint16_t>(TraceEventType::MemoryWrite);
    ev.core_id = 1;
    ev.exception_level = 1;
    ev.address_space = 1;
    ev.access_size = 4;
    ev.reserved = 0;

    std::uint8_t buf[TRACE_EVENT_DISK_SIZE];
    TEST_ASSERT(serialize_trace_event(ev, buf, sizeof(buf)) == TraceFileError::Success);

    TraceEvent decoded{};
    TEST_ASSERT(deserialize_trace_event(buf, sizeof(buf), decoded) == TraceFileError::Success);
    TEST_ASSERT(decoded == ev);
    std::cout << "PASS\n";
}

void test_3_roundtrip_all_13_fields() {
    std::cout << "  Test 3: Round-trip all 13 fields with distinct patterns... ";
    TraceEvent ev{};
    ev.sequence_number    = 0x1122334455667788ULL;
    ev.virtual_time       = 0x99AABBCCDDEEFF00ULL;
    ev.pc                 = 0x0102030405060708ULL;
    ev.address            = 0x090A0B0C0D0E0F10ULL;
    ev.data               = 0xF1E2D3C4B5A69788ULL;
    ev.opcode_or_syndrome = 0x12345678u;
    ev.flags              = 0x87654321u;
    ev.event_type         = static_cast<std::uint16_t>(TraceEventType::InstructionRetired);
    ev.core_id            = 0x0403u;
    ev.exception_level    = 2;
    ev.address_space      = 3;
    ev.access_size        = 8;
    ev.reserved           = 0;

    std::uint8_t buf[64];
    TEST_ASSERT(serialize_trace_event(ev, buf, sizeof(buf)) == TraceFileError::Success);

    TraceEvent out{};
    TEST_ASSERT(deserialize_trace_event(buf, sizeof(buf), out) == TraceFileError::Success);
    TEST_ASSERT(out == ev);
    std::cout << "PASS\n";
}

void test_4_verify_64_byte_offsets() {
    std::cout << "  Test 4: Verify 64 byte offsets on disk... ";
    TraceEvent ev{};
    ev.sequence_number = 0x01;
    ev.virtual_time = 0x02;
    ev.pc = 0x03;
    ev.address = 0x04;
    ev.data = 0x05;
    ev.opcode_or_syndrome = 0x06;
    ev.flags = 0x07;
    ev.event_type = 1;
    ev.core_id = 0x09;
    ev.exception_level = 1;
    ev.address_space = 2;
    ev.access_size = 4;
    ev.reserved = 0;

    std::uint8_t buf[64];
    std::memset(buf, 0xFF, sizeof(buf));
    TEST_ASSERT(serialize_trace_event(ev, buf, sizeof(buf)) == TraceFileError::Success);

    // Verify offsets match specification
    TEST_ASSERT(buf[0] == 0x01); // sequence_number
    TEST_ASSERT(buf[8] == 0x02); // virtual_time
    TEST_ASSERT(buf[16] == 0x03); // pc
    TEST_ASSERT(buf[24] == 0x04); // address
    TEST_ASSERT(buf[32] == 0x05); // data
    TEST_ASSERT(buf[40] == 0x06); // opcode_or_syndrome
    TEST_ASSERT(buf[44] == 0x07); // flags
    TEST_ASSERT(buf[48] == 0x01); // event_type
    TEST_ASSERT(buf[50] == 0x09); // core_id
    TEST_ASSERT(buf[52] == 0x01); // exception_level
    TEST_ASSERT(buf[53] == 0x02); // address_space
    TEST_ASSERT(buf[54] == 0x04); // access_size
    TEST_ASSERT(buf[56] == 0x00); // reserved
    std::cout << "PASS\n";
}

void test_5_verify_little_endian_representation() {
    std::cout << "  Test 5: Explicit Little-Endian encoding... ";
    TraceEvent ev{};
    ev.sequence_number = 0x0102030405060708ULL;
    ev.opcode_or_syndrome = 0xAABBCCDDu;
    ev.event_type = static_cast<std::uint16_t>(TraceEventType::InstructionRetired);
    ev.core_id = 0x1122u;

    std::uint8_t buf[64];
    TEST_ASSERT(serialize_trace_event(ev, buf, sizeof(buf)) == TraceFileError::Success);

    // Sequence number (0x0102030405060708) in LE must be: 08 07 06 05 04 03 02 01
    TEST_ASSERT(buf[0] == 0x08 && buf[1] == 0x07 && buf[2] == 0x06 && buf[3] == 0x05 &&
                buf[4] == 0x04 && buf[5] == 0x03 && buf[6] == 0x02 && buf[7] == 0x01);

    // Opcode (0xAABBCCDD) in LE must be: DD CC BB AA
    TEST_ASSERT(buf[40] == 0xDD && buf[41] == 0xCC && buf[42] == 0xBB && buf[43] == 0xAA);

    // Core ID (0x1122) in LE must be: 22 11
    TEST_ASSERT(buf[50] == 0x22 && buf[51] == 0x11);
    std::cout << "PASS\n";
}

void test_6_zero_valued_fields() {
    std::cout << "  Test 6: Zero-valued event serialization... ";
    TraceEvent ev{}; // all zero
    std::uint8_t buf[64];
    TEST_ASSERT(serialize_trace_event(ev, buf, sizeof(buf)) == TraceFileError::Success);

    for (int i = 0; i < 64; ++i) {
        TEST_ASSERT(buf[i] == 0x00);
    }

    TraceEvent out{};
    TEST_ASSERT(deserialize_trace_event(buf, sizeof(buf), out) == TraceFileError::Success);
    TEST_ASSERT(out == ev);
    std::cout << "PASS (all 64 bytes zero)\n";
}

void test_7_maximum_integer_values() {
    std::cout << "  Test 7: Maximum integer values... ";
    TraceEvent ev{};
    ev.sequence_number    = 0xFFFFFFFFFFFFFFFFULL;
    ev.virtual_time       = 0xFFFFFFFFFFFFFFFFULL;
    ev.pc                 = 0xFFFFFFFFFFFFFFFFULL;
    ev.address            = 0xFFFFFFFFFFFFFFFFULL;
    ev.data               = 0xFFFFFFFFFFFFFFFFULL;
    ev.opcode_or_syndrome = 0xFFFFFFFFu;
    ev.flags              = 0xFFFFFFFFu;
    ev.event_type         = static_cast<std::uint16_t>(TraceEventType::TimerEvent);
    ev.core_id            = 0xFFFFu;
    ev.exception_level    = 3;
    ev.address_space      = 3;
    ev.access_size        = 0xFFFFu;
    ev.reserved           = 0;

    std::uint8_t buf[64];
    TEST_ASSERT(serialize_trace_event(ev, buf, sizeof(buf)) == TraceFileError::Success);

    TraceEvent out{};
    TEST_ASSERT(deserialize_trace_event(buf, sizeof(buf), out) == TraceFileError::Success);
    TEST_ASSERT(out == ev);
    std::cout << "PASS\n";
}

void test_8_reserved_field_rejection() {
    std::cout << "  Test 8: Reserved field non-zero rejection... ";
    TraceEvent ev{};
    std::uint8_t buf[64];
    TEST_ASSERT(serialize_trace_event(ev, buf, sizeof(buf)) == TraceFileError::Success);

    // Corrupt reserved field at offset 56
    buf[56] = 0x01;
    TraceEvent out{};
    TEST_ASSERT(deserialize_trace_event(buf, sizeof(buf), out) == TraceFileError::InvalidReservedField);
    std::cout << "PASS (rejected non-zero reserved)\n";
}

void test_9_reject_unsupported_event_type() {
    std::cout << "  Test 9: Reject unsupported event types... ";
    TraceEvent ev{};
    ev.event_type = 999; // invalid/unsupported

    std::uint8_t buf[64];
    TEST_ASSERT(serialize_trace_event(ev, buf, sizeof(buf)) == TraceFileError::InvalidEventType);

    ev.event_type = 1;
    TEST_ASSERT(serialize_trace_event(ev, buf, sizeof(buf)) == TraceFileError::Success);
    // Corrupt event type in buffer
    buf[48] = 0xFF;
    buf[49] = 0x00; // 255 (reserved diag)
    TraceEvent out{};
    TEST_ASSERT(deserialize_trace_event(buf, sizeof(buf), out) == TraceFileError::InvalidEventType);
    std::cout << "PASS\n";
}

void test_10_reject_invalid_enums() {
    std::cout << "  Test 10: Reject invalid EL and address space... ";
    TraceEvent ev{};
    ev.exception_level = 4; // invalid (> 3)
    std::uint8_t buf[64];
    TEST_ASSERT(serialize_trace_event(ev, buf, sizeof(buf)) == TraceFileError::InvalidPrivilegeLevel);

    ev.exception_level = 1;
    ev.address_space = 5; // invalid (> 3)
    TEST_ASSERT(serialize_trace_event(ev, buf, sizeof(buf)) == TraceFileError::InvalidAddressSpace);
    std::cout << "PASS\n";
}

void test_11_schema_version_invariants() {
    std::cout << "  Test 11: Schema version constants... ";
    TEST_ASSERT(TRACE_SCHEMA_VERSION == 0x00010000u);
    TEST_ASSERT(TRACE_FILE_FORMAT_VERSION == 1);
    TEST_ASSERT(TRACE_FILE_HEADER_SIZE == 64);
    TEST_ASSERT(TRACE_BLOCK_HEADER_SIZE == 32);
    TEST_ASSERT(TRACE_FILE_FOOTER_SIZE == 64);
    std::cout << "PASS\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 52 Stage 4: Trace Serializer Tests ===\n\n";

    try {
        test_1_crc32c_test_vectors();
        test_2_encode_decode_known_event();
        test_3_roundtrip_all_13_fields();
        test_4_verify_64_byte_offsets();
        test_5_verify_little_endian_representation();
        test_6_zero_valued_fields();
        test_7_maximum_integer_values();
        test_8_reserved_field_rejection();
        test_9_reject_unsupported_event_type();
        test_10_reject_invalid_enums();
        test_11_schema_version_invariants();

        std::cout << "\n=== All 11 Serializer Tests PASSED Successfully! ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n=== Test FAILED with exception: " << e.what() << " ===\n";
        return 1;
    }
}
