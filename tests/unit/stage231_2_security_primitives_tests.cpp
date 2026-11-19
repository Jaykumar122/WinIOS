#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <cmath>
#include <cstdint>
#include <limits>

#include "security/security_utils.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Security Primitives Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::security;

// 1. CSPRNG tests
static void test_csprng_randomness() {
    std::cout << "[Stage 231.2] Testing SecureRandom CSPRNG...\n";

    // 1.1 Edge cases
    TEST_ASSERT(SecureRandom::get_random_bytes(nullptr, 0) == true);
    TEST_ASSERT(SecureRandom::get_random_bytes(nullptr, 16) == false);

    // 1.2 Uniqueness of 64-bit randoms
    std::set<std::uint64_t> u64_set;
    for (int i = 0; i < 1000; ++i) {
        std::uint64_t val = SecureRandom::get_random_u64();
        TEST_ASSERT(u64_set.insert(val).second); // Must all be unique
    }

    // 1.3 Uniqueness of 32-bit randoms
    std::set<std::uint32_t> u32_set;
    for (int i = 0; i < 1000; ++i) {
        std::uint32_t val = SecureRandom::get_random_u32();
        TEST_ASSERT(u32_set.insert(val).second); // Must all be unique
    }

    // 1.4 Random hex strings
    TEST_ASSERT(SecureRandom::get_random_hex(0).empty());
    std::string hex16 = SecureRandom::get_random_hex(16);
    TEST_ASSERT(hex16.size() == 32);
    for (char c : hex16) {
        TEST_ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    std::string hex16_2 = SecureRandom::get_random_hex(16);
    TEST_ASSERT(hex16 != hex16_2);

    // 1.5 Statistical spread check (basic uniformity check across 10,000 bytes)
    std::vector<std::uint8_t> buffer(10000);
    TEST_ASSERT(SecureRandom::get_random_bytes(buffer.data(), buffer.size()));
    double sum = 0.0;
    std::vector<int> freq(256, 0);
    for (std::uint8_t b : buffer) {
        sum += b;
        freq[b]++;
    }
    double mean = sum / buffer.size();
    // Theoretical mean of uniform 0..255 is 127.5.
    // Over 10,000 samples, mean should almost certainly be within [120, 135].
    TEST_ASSERT(mean >= 120.0 && mean <= 135.0);

    // Every bucket in 256 should have at least 1 sample with 10,000 rolls (expected ~39)
    int non_zero_buckets = 0;
    for (int count : freq) {
        if (count > 0) non_zero_buckets++;
    }
    TEST_ASSERT(non_zero_buckets >= 250); // Almost all 256 byte values present
}

// 2. Constant-time equality tests
static void test_constant_time_equals() {
    std::cout << "[Stage 231.2] Testing constant_time_equals...\n";

    // 2.1 Pointer edge cases
    TEST_ASSERT(constant_time_equals(nullptr, nullptr, 0));
    TEST_ASSERT(!constant_time_equals(nullptr, "a", 1));
    TEST_ASSERT(!constant_time_equals("a", nullptr, 1));

    // 2.2 Raw buffers
    const char buf1[] = "TheQuickBrownFox1234567890!@#$%^";
    const char buf2[] = "TheQuickBrownFox1234567890!@#$%^";
    char buf3[] = "TheQuickBrownFox1234567890!@#$%^";
    const std::size_t len = sizeof(buf1);

    TEST_ASSERT(constant_time_equals(buf1, buf2, len));

    // Differ at index 0
    buf3[0] ^= 0x01;
    TEST_ASSERT(!constant_time_equals(buf1, buf3, len));
    buf3[0] ^= 0x01;

    // Differ in middle
    buf3[16] ^= 0x80;
    TEST_ASSERT(!constant_time_equals(buf1, buf3, len));
    buf3[16] ^= 0x80;

    // Differ at last byte
    buf3[len - 1] ^= 0x02;
    TEST_ASSERT(!constant_time_equals(buf1, buf3, len));
    buf3[len - 1] ^= 0x02;

    // 2.3 std::string overloads
    std::string s1 = "3d1255a8027dc8e8f85f1cfa8c821107519ff09c0663488f7b0ecfa329615cb3";
    std::string s2 = "3d1255a8027dc8e8f85f1cfa8c821107519ff09c0663488f7b0ecfa329615cb3";
    std::string s3 = "3d1255a8027dc8e8f85f1cfa8c821107519ff09c0663488f7b0ecfa329615cb4";
    std::string s4 = "short";

    TEST_ASSERT(constant_time_equals(s1, s2));
    TEST_ASSERT(!constant_time_equals(s1, s3));
    TEST_ASSERT(!constant_time_equals(s1, s4));
    TEST_ASSERT(constant_time_equals(std::string(""), std::string("")));

    // 2.4 std::vector overloads
    std::vector<std::uint8_t> v1 = {1, 2, 3, 4, 5};
    std::vector<std::uint8_t> v2 = {1, 2, 3, 4, 5};
    std::vector<std::uint8_t> v3 = {1, 2, 3, 4, 6};
    std::vector<std::uint8_t> v4 = {1, 2, 3};

    TEST_ASSERT(constant_time_equals(v1, v2));
    TEST_ASSERT(!constant_time_equals(v1, v3));
    TEST_ASSERT(!constant_time_equals(v1, v4));
}

// 3. Memory zeroing and SecureBuffer
static void test_secure_buffer_and_zeroing() {
    std::cout << "[Stage 231.2] Testing secure_zero_memory and SecureBuffer...\n";

    // 3.1 secure_zero_memory
    std::vector<std::uint8_t> test_mem(128, 0xEF);
    secure_zero_memory(test_mem.data(), test_mem.size());
    for (std::uint8_t b : test_mem) {
        TEST_ASSERT(b == 0);
    }

    // 3.2 SecureBuffer initialization and wipe on clear
    {
        SecureBuffer sb(64);
        TEST_ASSERT(sb.size() == 64);
        TEST_ASSERT(!sb.empty());
        for (std::size_t i = 0; i < sb.size(); ++i) {
            TEST_ASSERT(sb.data()[i] == 0);
            sb.data()[i] = static_cast<std::uint8_t>(i + 1);
        }

        // Pointer to raw heap data
        std::uint8_t* raw_ptr = sb.data();
        TEST_ASSERT(raw_ptr[0] == 1);

        sb.clear();
        TEST_ASSERT(sb.empty());
        TEST_ASSERT(sb.size() == 0);
        TEST_ASSERT(sb.data() == nullptr);
    }

    // 3.3 SecureBuffer copy from memory
    {
        const std::uint8_t secret[] = {0xDE, 0xAD, 0xBE, 0xEF};
        SecureBuffer sb(secret, sizeof(secret));
        TEST_ASSERT(sb.size() == 4);
        TEST_ASSERT(sb.data()[0] == 0xDE);
        TEST_ASSERT(sb.data()[1] == 0xAD);
        TEST_ASSERT(sb.data()[2] == 0xBE);
        TEST_ASSERT(sb.data()[3] == 0xEF);

        // Move constructor
        SecureBuffer moved(std::move(sb));
        TEST_ASSERT(sb.empty());
        TEST_ASSERT(moved.size() == 4);
        TEST_ASSERT(moved.data()[2] == 0xBE);

        // Move assignment
        SecureBuffer target;
        target = std::move(moved);
        TEST_ASSERT(moved.empty());
        TEST_ASSERT(target.size() == 4);
        TEST_ASSERT(target.data()[3] == 0xEF);
    }

    // 3.4 Resize
    {
        SecureBuffer sb(16);
        for (std::size_t i = 0; i < 16; ++i) sb.data()[i] = 0x55;
        sb.resize(32);
        TEST_ASSERT(sb.size() == 32);
        for (std::size_t i = 0; i < 16; ++i) TEST_ASSERT(sb.data()[i] == 0x55);
        for (std::size_t i = 16; i < 32; ++i) TEST_ASSERT(sb.data()[i] == 0);

        sb.resize(8);
        TEST_ASSERT(sb.size() == 8);
        for (std::size_t i = 0; i < 8; ++i) TEST_ASSERT(sb.data()[i] == 0x55);

        sb.resize(0);
        TEST_ASSERT(sb.empty());
    }
}

// 4. Safe integer arithmetic tests
static void test_checked_arithmetic() {
    std::cout << "[Stage 231.2] Testing safe integer arithmetic...\n";

    // 4.1 Unsigned addition
    {
        std::uint64_t max_u64 = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t out = 0;
        TEST_ASSERT(checked_add<std::uint64_t>(100, 200, out) && out == 300);
        TEST_ASSERT(!checked_add<std::uint64_t>(max_u64, 1, out));
        TEST_ASSERT(!checked_add<std::uint64_t>(max_u64 - 5, 6, out));
        TEST_ASSERT(checked_add<std::uint64_t>(max_u64 - 5, 5, out) && out == max_u64);

        auto opt = checked_add<std::uint64_t>(max_u64, 1);
        TEST_ASSERT(!opt.has_value());
        opt = checked_add<std::uint64_t>(10, 20);
        TEST_ASSERT(opt.has_value() && *opt == 30);
    }

    // 4.2 Signed addition
    {
        std::int32_t max_i32 = std::numeric_limits<std::int32_t>::max();
        std::int32_t min_i32 = std::numeric_limits<std::int32_t>::min();
        std::int32_t out = 0;
        TEST_ASSERT(checked_add<std::int32_t>(10, -5, out) && out == 5);
        TEST_ASSERT(!checked_add<std::int32_t>(max_i32, 1, out));
        TEST_ASSERT(!checked_add<std::int32_t>(min_i32, -1, out));
        TEST_ASSERT(checked_add<std::int32_t>(max_i32 - 1, 1, out) && out == max_i32);
    }

    // 4.3 Unsigned subtraction
    {
        std::uint32_t out = 0;
        TEST_ASSERT(checked_sub<std::uint32_t>(100, 40, out) && out == 60);
        TEST_ASSERT(!checked_sub<std::uint32_t>(40, 100, out));
        TEST_ASSERT(checked_sub<std::uint32_t>(0, 0, out) && out == 0);
    }

    // 4.4 Signed subtraction
    {
        std::int32_t max_i32 = std::numeric_limits<std::int32_t>::max();
        std::int32_t min_i32 = std::numeric_limits<std::int32_t>::min();
        std::int32_t out = 0;
        TEST_ASSERT(checked_sub<std::int32_t>(5, 10, out) && out == -5);
        TEST_ASSERT(!checked_sub<std::int32_t>(min_i32, 1, out));
        TEST_ASSERT(!checked_sub<std::int32_t>(max_i32, -1, out));
    }

    // 4.5 Unsigned multiplication
    {
        std::uint64_t max_u64 = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t out = 0;
        TEST_ASSERT(checked_mul<std::uint64_t>(0, max_u64, out) && out == 0);
        TEST_ASSERT(checked_mul<std::uint64_t>(max_u64, 0, out) && out == 0);
        TEST_ASSERT(checked_mul<std::uint64_t>(10, 20, out) && out == 200);
        TEST_ASSERT(!checked_mul<std::uint64_t>(max_u64 / 2 + 1, 2, out));
        TEST_ASSERT(checked_mul<std::uint64_t>(max_u64 / 2, 2, out) && out == (max_u64 / 2) * 2);

        auto opt = checked_mul<std::uint64_t>(max_u64, 2);
        TEST_ASSERT(!opt.has_value());
    }

    // 4.6 Signed multiplication
    {
        std::int32_t max_i32 = std::numeric_limits<std::int32_t>::max();
        std::int32_t min_i32 = std::numeric_limits<std::int32_t>::min();
        std::int32_t out = 0;
        TEST_ASSERT(checked_mul<std::int32_t>(-5, 4, out) && out == -20);
        TEST_ASSERT(checked_mul<std::int32_t>(-5, -4, out) && out == 20);
        TEST_ASSERT(!checked_mul<std::int32_t>(max_i32, 2, out));
        TEST_ASSERT(!checked_mul<std::int32_t>(min_i32, -1, out));
        TEST_ASSERT(!checked_mul<std::int32_t>(min_i32, 2, out));
    }
}

// 5. BinaryReader bounds checking and slice tests
static void test_binary_reader() {
    std::cout << "[Stage 231.2] Testing BinaryReader...\n";

    // Setup payload:
    // [0] = 0x42 (u8)
    // [1..2] = 0x1234 (u16 LE: 0x34, 0x12)
    // [3..6] = 0xA1B2C3D4 (u32 LE: 0xD4, 0xC3, 0xB2, 0xA1)
    // [7..14] = 0x0102030405060708 (u64 LE: 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01)
    // [15..19] = "HELLO"
    std::vector<std::uint8_t> payload = {
        0x42,
        0x34, 0x12,
        0xD4, 0xC3, 0xB2, 0xA1,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        'H', 'E', 'L', 'L', 'O'
    };

    BinaryReader reader(payload.data(), payload.size());
    TEST_ASSERT(reader.total_size() == 20);
    TEST_ASSERT(reader.remaining() == 20);
    TEST_ASSERT(reader.offset() == 0);

    // Read u8
    std::uint8_t u8_val = 0;
    TEST_ASSERT(reader.read_u8(u8_val) && u8_val == 0x42);
    TEST_ASSERT(reader.offset() == 1);
    TEST_ASSERT(reader.remaining() == 19);

    // Read u16
    std::uint16_t u16_val = 0;
    TEST_ASSERT(reader.read_u16_le(u16_val) && u16_val == 0x1234);
    TEST_ASSERT(reader.offset() == 3);

    // Read u32
    std::uint32_t u32_val = 0;
    TEST_ASSERT(reader.read_u32_le(u32_val) && u32_val == 0xA1B2C3D4);
    TEST_ASSERT(reader.offset() == 7);

    // Read u64
    std::uint64_t u64_val = 0;
    TEST_ASSERT(reader.read_u64_le(u64_val) && u64_val == 0x0102030405060708ULL);
    TEST_ASSERT(reader.offset() == 15);

    // Read string
    std::string str_val;
    TEST_ASSERT(reader.read_string(str_val, 5) && str_val == "HELLO");
    TEST_ASSERT(reader.offset() == 20);
    TEST_ASSERT(reader.remaining() == 0);

    // Attempt read beyond EOF
    std::uint8_t extra_byte = 0;
    TEST_ASSERT(!reader.read_u8(extra_byte));
    TEST_ASSERT(!reader.read_bytes(&extra_byte, 1));
    TEST_ASSERT(!reader.has_bytes(1));
    TEST_ASSERT(!reader.skip(1));

    // Seek back to start
    TEST_ASSERT(reader.seek(0));
    TEST_ASSERT(reader.offset() == 0);
    TEST_ASSERT(reader.remaining() == 20);

    // Seek invalid offset
    TEST_ASSERT(!reader.seek(21));
    TEST_ASSERT(reader.offset() == 0); // Position unchanged

    // Skip
    TEST_ASSERT(reader.skip(7));
    TEST_ASSERT(reader.offset() == 7);

    // Read slice / span
    const std::uint8_t* slice_ptr = nullptr;
    TEST_ASSERT(reader.read_slice(slice_ptr, 8) && slice_ptr != nullptr);
    TEST_ASSERT(reader.offset() == 15);

    auto span = reader.read_span(5);
    TEST_ASSERT(span.size() == 5);
    TEST_ASSERT(span[0] == 'H' && span[4] == 'O');

    // Span out of bounds returns empty
    auto bad_span = reader.read_span(1);
    TEST_ASSERT(bad_span.empty());

    // Slice out of bounds fails
    TEST_ASSERT(!reader.read_slice(slice_ptr, 1));
    TEST_ASSERT(slice_ptr == nullptr);
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 231.2: Unified Security Primitives Tests    \n";
    std::cout << "====================================================\n";

    test_csprng_randomness();
    test_constant_time_equals();
    test_secure_buffer_and_zeroing();
    test_checked_arithmetic();
    test_binary_reader();

    std::cout << "\n[Stage 231.2] ALL TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
