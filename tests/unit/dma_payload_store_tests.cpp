// =============================================================================
// WinIOS Phase 52 Stage 32: DMA Payload Companion Store Unit Tests
// =============================================================================

#include "device/dma_payload_store.h"
#include "trace/trace_serializer.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#define TEST_ASSERT(expr) do { \
    if (!(expr)) { \
        std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)
#undef assert
#define assert(expr) TEST_ASSERT(expr)

using namespace winios::device;

namespace {

const std::filesystem::path kTestDir = std::filesystem::temp_directory_path() / "winios_dma_payload_tests";

void setup_test_dir() {
    std::filesystem::create_directories(kTestDir);
}

void cleanup_test_dir() {
    std::error_code ec;
    std::filesystem::remove_all(kTestDir, ec);
}

// -----------------------------------------------------------------------------
// Group 1: Companion Store Lifecycle, Magic & Version
// -----------------------------------------------------------------------------
void test_group_1_lifecycle() {
    std::cout << "[Group 1] Testing Companion Store Lifecycle, Magic & Version...\n";
    setup_test_dir();

    const auto file_path = kTestDir / "lifecycle_test.dma";

    DmaPayloadStore store;
    TEST_ASSERT(!store.is_open());
    TEST_ASSERT(store.record_count() == 0);
    TEST_ASSERT(store.total_payload_bytes() == 0);

    auto open_res = store.open_write(file_path);
    TEST_ASSERT(open_res.is_success());
    TEST_ASSERT(store.is_open());

    const std::vector<std::uint8_t> data1 = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    auto rec_res = store.record_payload(100, 1, 0x2000, data1.data(), data1.size());
    TEST_ASSERT(rec_res.is_success());
    TEST_ASSERT(store.record_count() == 1);
    TEST_ASSERT(store.total_payload_bytes() == data1.size());

    auto fin_res = store.finalize();
    TEST_ASSERT(fin_res.is_success());

    // Reopen for read
    DmaPayloadStore reader;
    auto read_res = reader.open_read(file_path);
    TEST_ASSERT(read_res.is_success());
    TEST_ASSERT(reader.record_count() == 1);
    TEST_ASSERT(reader.total_payload_bytes() == data1.size());

    const auto* rec = reader.find_payload(100);
    TEST_ASSERT(rec != nullptr);
    TEST_ASSERT(rec->global_sequence == 100);
    TEST_ASSERT(rec->descriptor_id == 1);
    TEST_ASSERT(rec->dst_paddr == 0x2000);
    TEST_ASSERT(rec->data == data1);
    TEST_ASSERT(rec->verify_crc());

    reader.clear();
    TEST_ASSERT(!reader.is_open());
    TEST_ASSERT(reader.record_count() == 0);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Group 2: Multiple Payloads with Variable Sizes and CRC32C Validation
// -----------------------------------------------------------------------------
void test_group_2_multi_payloads() {
    std::cout << "[Group 2] Testing Multiple Payloads and Variable Sizes...\n";
    const auto file_path = kTestDir / "multi_payload_test.dma";

    DmaPayloadStore store;
    TEST_ASSERT(store.open_write(file_path).is_success());

    std::vector<std::vector<std::uint8_t>> payloads;
    for (std::size_t i = 0; i < 20; ++i) {
        const std::size_t sz = (i + 1) * 128;
        std::vector<std::uint8_t> buf(sz);
        for (std::size_t b = 0; b < sz; ++b) {
            buf[b] = static_cast<std::uint8_t>((i * 17 + b * 31) & 0xFF);
        }
        payloads.push_back(buf);
        auto res = store.record_payload(1000 + i, 50 + i, 0x10000 + i * 0x1000, buf.data(), buf.size());
        TEST_ASSERT(res.is_success());
    }
    TEST_ASSERT(store.finalize().is_success());

    DmaPayloadStore reader;
    TEST_ASSERT(reader.open_read(file_path).is_success());
    TEST_ASSERT(reader.record_count() == 20);

    for (std::size_t i = 0; i < 20; ++i) {
        const auto* rec = reader.find_payload(1000 + i);
        TEST_ASSERT(rec != nullptr);
        TEST_ASSERT(rec->global_sequence == 1000 + i);
        TEST_ASSERT(rec->descriptor_id == 50 + i);
        TEST_ASSERT(rec->dst_paddr == 0x10000 + i * 0x1000);
        TEST_ASSERT(rec->data == payloads[i]);
        TEST_ASSERT(rec->verify_crc());
    }

    TEST_ASSERT(reader.find_payload(999) == nullptr);
    TEST_ASSERT(reader.find_payload(1020) == nullptr);
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Group 3: Adversarial File Corruptions
// -----------------------------------------------------------------------------
void test_group_3_adversarial_corruptions() {
    std::cout << "[Group 3] Testing Adversarial File Corruptions...\n";

    // 1. Non-existent file
    {
        DmaPayloadStore store;
        auto res = store.open_read(kTestDir / "does_not_exist.dma");
        TEST_ASSERT(res.is_failure());
        TEST_ASSERT(res.code == DmaPayloadStoreError::FileNotFound);
    }

    // 2. Truncated header
    {
        const auto bad_file = kTestDir / "trunc_header.dma";
        std::ofstream f(bad_file, std::ios::binary);
        const std::uint8_t short_bytes[10] = {1, 2, 3, 4, 5};
        f.write(reinterpret_cast<const char*>(short_bytes), sizeof(short_bytes));
        f.close();

        DmaPayloadStore store;
        auto res = store.open_read(bad_file);
        TEST_ASSERT(res.is_failure());
        TEST_ASSERT(res.code == DmaPayloadStoreError::TruncatedPayload);
    }

    // 3. Bad magic
    {
        const auto bad_file = kTestDir / "bad_magic.dma";
        std::vector<std::uint8_t> hbuf(kDmaDataHeaderSize, 0);
        const std::uint64_t bad_magic = 0xDEADBEEFULL;
        std::memcpy(hbuf.data(), &bad_magic, 8);

        std::ofstream f(bad_file, std::ios::binary);
        f.write(reinterpret_cast<const char*>(hbuf.data()), hbuf.size());
        f.close();

        DmaPayloadStore store;
        auto res = store.open_read(bad_file);
        TEST_ASSERT(res.is_failure());
        TEST_ASSERT(res.code == DmaPayloadStoreError::InvalidMagic);
    }

    // 4. Bad version
    {
        const auto bad_file = kTestDir / "bad_version.dma";
        std::vector<std::uint8_t> hbuf(kDmaDataHeaderSize, 0);
        std::memcpy(hbuf.data(), &kDmaDataFileMagic, 8);
        const std::uint16_t bad_ver = 999;
        std::memcpy(hbuf.data() + 8, &bad_ver, 2);

        std::ofstream f(bad_file, std::ios::binary);
        f.write(reinterpret_cast<const char*>(hbuf.data()), hbuf.size());
        f.close();

        DmaPayloadStore store;
        auto res = store.open_read(bad_file);
        TEST_ASSERT(res.is_failure());
        TEST_ASSERT(res.code == DmaPayloadStoreError::UnsupportedVersion);
    }

    // 5. Corrupted payload bytes (CRC mismatch)
    {
        const auto base_file = kTestDir / "crc_test.dma";
        {
            DmaPayloadStore store;
            TEST_ASSERT(store.open_write(base_file).is_success());
            const std::vector<std::uint8_t> d = {10, 20, 30, 40, 50};
            TEST_ASSERT(store.record_payload(42, 1, 0x4000, d.data(), d.size()).is_success());
            TEST_ASSERT(store.finalize().is_success());
        }

        // Corrupt a byte in the payload
        {
            std::fstream f(base_file, std::ios::in | std::ios::out | std::ios::binary);
            f.seekp(kDmaDataHeaderSize + kDmaDataEntryHeaderSize + 2);
            const char corrupted_byte = 0x7F;
            f.write(&corrupted_byte, 1);
            f.close();
        }

        DmaPayloadStore store;
        auto res = store.open_read(base_file);
        TEST_ASSERT(res.is_failure());
        TEST_ASSERT(res.code == DmaPayloadStoreError::PayloadCrcMismatch);
    }
    std::cout << "  -> PASSED\n";
}

// -----------------------------------------------------------------------------
// Group 4: In-Memory Payload Store Direct Operations
// -----------------------------------------------------------------------------
void test_group_4_in_memory() {
    std::cout << "[Group 4] Testing In-Memory Payload Store Direct Operations...\n";

    DmaPayloadStore store;
    DmaPayloadRecord rec{};
    rec.global_sequence = 505;
    rec.descriptor_id = 9;
    rec.dst_paddr = 0x8000;
    rec.data = {1, 2, 3, 4, 5, 6, 7, 8};
    rec.payload_crc32c = winios::trace::calculate_crc32c(rec.data.data(), rec.data.size());

    TEST_ASSERT(rec.verify_crc());
    store.insert_in_memory_payload(rec);

    TEST_ASSERT(store.record_count() == 1);
    TEST_ASSERT(store.total_payload_bytes() == 8);

    const auto* found = store.find_payload(505);
    TEST_ASSERT(found != nullptr);
    TEST_ASSERT(found->data == rec.data);

    store.clear();
    TEST_ASSERT(store.record_count() == 0);
    TEST_ASSERT(store.find_payload(505) == nullptr);
    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << "=== Running DMA Payload Store Unit Tests ===\n";
    test_group_1_lifecycle();
    test_group_2_multi_payloads();
    test_group_3_adversarial_corruptions();
    test_group_4_in_memory();
    cleanup_test_dir();
    std::cout << "=== All DMA Payload Store Unit Tests PASSED ===\n";
    return 0;
}
