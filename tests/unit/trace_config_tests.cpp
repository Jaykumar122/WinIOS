#include "trace/trace_config.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

using namespace winios::trace;

namespace {

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at line " << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while(0)

void test_1_default_config() {
    std::cout << "  Test 1: Default configuration... ";
    TraceConfig cfg;
    TEST_ASSERT(!cfg.enabled);
    TEST_ASSERT(!cfg.file_output_enabled);
    TEST_ASSERT(cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::Success);
    std::cout << "PASS\n";
}

void test_2_valid_enabled_config() {
    std::cout << "  Test 2: Valid enabled configuration... ";
    TraceConfig cfg = TraceConfig::default_enabled_config();
    TEST_ASSERT(cfg.enabled);
    TEST_ASSERT(cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::Success);
    std::cout << "PASS\n";
}

void test_3_invalid_ring_capacity() {
    std::cout << "  Test 3: Invalid ring capacity... ";
    TraceConfig cfg = TraceConfig::default_enabled_config();

    cfg.ring_capacity = 0;
    TEST_ASSERT(!cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::InvalidRingCapacity);

    cfg.ring_capacity = 1; // Less than MIN_RING_BUFFER_CAPACITY (2)
    TEST_ASSERT(!cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::InvalidRingCapacity);

    cfg.ring_capacity = MAX_RING_BUFFER_CAPACITY + 1;
    TEST_ASSERT(!cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::InvalidRingCapacity);
    std::cout << "PASS\n";
}

void test_4_invalid_batch_size() {
    std::cout << "  Test 4: Invalid batch size... ";
    TraceConfig cfg = TraceConfig::default_enabled_config();

    cfg.writer_batch_size = 0;
    TEST_ASSERT(!cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::InvalidBatchSize);

    cfg.writer_batch_size = MAX_EVENTS_PER_BLOCK + 1; // > 4096
    TEST_ASSERT(!cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::InvalidBatchSize);

    cfg.ring_capacity = 16;
    cfg.file_output_enabled = true;
    cfg.output_path = "test.wtr";
    cfg.writer_batch_size = 32; // > ring_capacity
    TEST_ASSERT(!cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::InvalidBatchSize);
    std::cout << "PASS\n";
}

void test_5_empty_event_types() {
    std::cout << "  Test 5: Empty event types filter... ";
    TraceConfig cfg = TraceConfig::default_enabled_config();

    cfg.event_types.reset();
    TEST_ASSERT(!cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::EmptyEventTypeFilter);
    std::cout << "PASS\n";
}

void test_6_empty_core_mask() {
    std::cout << "  Test 6: Empty core mask... ";
    TraceConfig cfg = TraceConfig::default_enabled_config();

    cfg.core_mask = 0;
    TEST_ASSERT(!cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::EmptyCoreMask);
    std::cout << "PASS\n";
}

void test_7_file_output_path_validation() {
    std::cout << "  Test 7: File output path validation... ";
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.file_output_enabled = true;

    // Empty path
    cfg.output_path = "";
    TEST_ASSERT(!cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::EmptyOutputPath);

    // Valid path
    cfg.output_path = "C:/tmp/test_trace.wtr";
    TEST_ASSERT(cfg.is_valid());
    TEST_ASSERT(cfg.validate() == TraceConfigError::Success);
    std::cout << "PASS\n";
}

void test_8_event_type_filter_operations() {
    std::cout << "  Test 8: EventTypeFilter operations... ";
    EventTypeFilter filter;
    TEST_ASSERT(filter.none());
    TEST_ASSERT(filter.count() == 0);

    filter.set(TraceEventType::InstructionRetired, true);
    filter.set(TraceEventType::BranchTaken, true);
    filter.set(TraceEventType::DroppedMarker, true); // type 200 (> 64)

    TEST_ASSERT(filter.any());
    TEST_ASSERT(filter.count() == 3);
    TEST_ASSERT(filter.test(TraceEventType::InstructionRetired));
    TEST_ASSERT(filter.test(TraceEventType::BranchTaken));
    TEST_ASSERT(filter.test(TraceEventType::DroppedMarker));
    TEST_ASSERT(!filter.test(TraceEventType::MemoryRead));

    // Disable one
    filter.set(TraceEventType::BranchTaken, false);
    TEST_ASSERT(!filter.test(TraceEventType::BranchTaken));
    TEST_ASSERT(filter.count() == 2);

    // Set all
    filter.set_all(true);
    TEST_ASSERT(filter.count() == 256);
    TEST_ASSERT(filter.test(TraceEventType::MemoryRead));

    // Reset
    filter.reset();
    TEST_ASSERT(filter.none());
    TEST_ASSERT(filter.count() == 0);

    // from_u64_mask
    filter.from_u64_mask(0x3); // bits 0 and 1
    TEST_ASSERT(filter.test(0));
    TEST_ASSERT(filter.test(1));
    TEST_ASSERT(!filter.test(2));
    TEST_ASSERT(filter.to_u64_mask() == 0x3);
    std::cout << "PASS\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 52 Stage 5: Trace Config Tests ===\n\n";

    test_1_default_config();
    test_2_valid_enabled_config();
    test_3_invalid_ring_capacity();
    test_4_invalid_batch_size();
    test_5_empty_event_types();
    test_6_empty_core_mask();
    test_7_file_output_path_validation();
    test_8_event_type_filter_operations();

    std::cout << "\n=== All 8 Trace Config Tests PASSED Successfully! ===\n";
    return 0;
}
