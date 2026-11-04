#include "trace/trace_types.h"
#include "trace/trace_event.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <set>
#include <vector>

using namespace winios::trace;

namespace {

void test_1_event_size() {
    std::cout << "  Running Test 1: Event Size... ";
    static_assert(sizeof(TraceEvent) == 64, "TraceEvent size must be exactly 64 bytes");
    assert(sizeof(TraceEvent) == 64);
    std::cout << "PASS (sizeof = 64 bytes)\n";
}

void test_2_event_alignment() {
    std::cout << "  Running Test 2: Event Alignment... ";
    static_assert(alignof(TraceEvent) == 64, "TraceEvent alignment must be 64 bytes");
    assert(alignof(TraceEvent) == 64);

    // Verify stack alignment
    TraceEvent stack_ev{};
    assert(reinterpret_cast<std::uintptr_t>(&stack_ev) % 64 == 0);

    // Verify array contiguous cache-line alignment
    TraceEvent event_array[4]{};
    assert(reinterpret_cast<std::uintptr_t>(&event_array[0]) % 64 == 0);
    assert(reinterpret_cast<std::uintptr_t>(&event_array[1]) % 64 == 0);
    assert(reinterpret_cast<std::uintptr_t>(&event_array[2]) % 64 == 0);
    assert(reinterpret_cast<std::uintptr_t>(&event_array[3]) % 64 == 0);
    std::cout << "PASS (alignof = 64 bytes)\n";
}

void test_3_standard_layout() {
    std::cout << "  Running Test 3: Standard Layout Property... ";
    static_assert(std::is_standard_layout_v<TraceEvent>, "TraceEvent must be standard layout");
    assert(std::is_standard_layout_v<TraceEvent>);
    std::cout << "PASS\n";
}

void test_4_trivially_copyable() {
    std::cout << "  Running Test 4: Trivially Copyable Property... ";
    static_assert(std::is_trivially_copyable_v<TraceEvent>, "TraceEvent must be trivially copyable");
    assert(std::is_trivially_copyable_v<TraceEvent>);
    std::cout << "PASS\n";
}

void test_5_enum_widths() {
    std::cout << "  Running Test 5: Enum Underlying Widths... ";
    static_assert(sizeof(TraceEventType) == 2, "TraceEventType must be 2 bytes (uint16_t)");
    static_assert(sizeof(TraceEventFlags) == 4, "TraceEventFlags must be 4 bytes (uint32_t)");
    static_assert(sizeof(TraceExecutionMode) == 1, "TraceExecutionMode must be 1 byte (uint8_t)");
    static_assert(sizeof(TracePrivilegeLevel) == 1, "TracePrivilegeLevel must be 1 byte (uint8_t)");
    static_assert(sizeof(TraceAddressSpace) == 1, "TraceAddressSpace must be 1 byte (uint8_t)");

    assert(sizeof(TraceEventType) == 2);
    assert(sizeof(TraceEventFlags) == 4);
    assert(sizeof(TraceExecutionMode) == 1);
    assert(sizeof(TracePrivilegeLevel) == 1);
    assert(sizeof(TraceAddressSpace) == 1);
    std::cout << "PASS\n";
}

void test_6_stable_event_identifiers() {
    std::cout << "  Running Test 6: Stable Event Identifier Values... ";
    assert(static_cast<std::uint16_t>(TraceEventType::Invalid) == 0);
    assert(static_cast<std::uint16_t>(TraceEventType::InstructionRetired) == 1);
    assert(static_cast<std::uint16_t>(TraceEventType::BranchTaken) == 2);
    assert(static_cast<std::uint16_t>(TraceEventType::ExceptionEntered) == 3);
    assert(static_cast<std::uint16_t>(TraceEventType::ExceptionReturned) == 4);
    assert(static_cast<std::uint16_t>(TraceEventType::InterruptDelivered) == 5);
    assert(static_cast<std::uint16_t>(TraceEventType::SystemRegisterAccess) == 6);
    assert(static_cast<std::uint16_t>(TraceEventType::MemoryRead) == 7);
    assert(static_cast<std::uint16_t>(TraceEventType::MemoryWrite) == 8);
    assert(static_cast<std::uint16_t>(TraceEventType::DebugEvent) == 9);
    assert(static_cast<std::uint16_t>(TraceEventType::CPUStarted) == 10);
    assert(static_cast<std::uint16_t>(TraceEventType::CPUStopped) == 11);
    assert(static_cast<std::uint16_t>(TraceEventType::ContextChanged) == 12);
    assert(static_cast<std::uint16_t>(TraceEventType::TimerEvent) == 13);
    assert(static_cast<std::uint16_t>(TraceEventType::DroppedMarker) == 200);
    assert(static_cast<std::uint16_t>(TraceEventType::TraceStart) == 201);
    assert(static_cast<std::uint16_t>(TraceEventType::TraceEnd) == 202);

    // Verify string conversion helpers
    assert(to_string(TraceEventType::InstructionRetired) == "InstructionRetired");
    assert(to_string(TraceEventType::MemoryRead) == "MemoryRead");
    assert(to_string(TraceEventType::Invalid) == "Invalid");
    std::cout << "PASS\n";
}

void test_7_flag_bit_positions() {
    std::cout << "  Running Test 7: Flag Bit Positions & Operators... ";
    assert(static_cast<std::uint32_t>(TraceEventFlags::None) == 0u);
    assert(static_cast<std::uint32_t>(TraceEventFlags::HasVirtualAddress) == (1u << 0));
    assert(static_cast<std::uint32_t>(TraceEventFlags::HasPhysicalAddress) == (1u << 1));
    assert(static_cast<std::uint32_t>(TraceEventFlags::HasData) == (1u << 2));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsPrivileged) == (1u << 3));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsWrite) == (1u << 4));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsTaken) == (1u << 5));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsSynchronous) == (1u << 6));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsExceptionReturn) == (1u << 7));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsCompressed) == (1u << 8));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsInterrupt) == (1u << 9));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsSingleStep) == (1u << 10));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsBreakpoint) == (1u << 11));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsWatchpoint) == (1u << 12));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsAtomic) == (1u << 13));
    assert(static_cast<std::uint32_t>(TraceEventFlags::IsDroppedMarker) == (1u << 14));

    // Operator tests
    TraceEventFlags combined = TraceEventFlags::HasVirtualAddress | TraceEventFlags::HasData | TraceEventFlags::IsWrite;
    assert(has_flag(combined, TraceEventFlags::HasVirtualAddress));
    assert(has_flag(combined, TraceEventFlags::HasData));
    assert(has_flag(combined, TraceEventFlags::IsWrite));
    assert(!has_flag(combined, TraceEventFlags::IsPrivileged));

    combined &= ~TraceEventFlags::HasData;
    assert(!has_flag(combined, TraceEventFlags::HasData));
    assert(has_flag(combined, TraceEventFlags::HasVirtualAddress));
    assert(has_flag(combined, TraceEventFlags::IsWrite));

    combined ^= TraceEventFlags::IsPrivileged;
    assert(has_flag(combined, TraceEventFlags::IsPrivileged));
    std::cout << "PASS\n";
}

void test_8_default_zero_initialization() {
    std::cout << "  Running Test 8: Default Zero Initialization... ";
    TraceEvent ev{};
    const auto* raw_bytes = reinterpret_cast<const std::uint8_t*>(&ev);
    for (std::size_t i = 0; i < sizeof(TraceEvent); ++i) {
        if (raw_bytes[i] != 0x00) {
            std::cerr << "Byte " << i << " is non-zero!\n";
            std::abort();
        }
    }
    assert(ev.sequence_number == 0);
    assert(ev.virtual_time == 0);
    assert(ev.pc == 0);
    assert(ev.address == 0);
    assert(ev.data == 0);
    assert(ev.opcode_or_syndrome == 0);
    assert(ev.flags == 0);
    assert(ev.event_type == 0);
    assert(ev.core_id == 0);
    assert(ev.exception_level == 0);
    assert(ev.address_space == 0);
    assert(ev.access_size == 0);
    assert(ev.reserved == 0);
    std::cout << "PASS (all 64 bytes are 0x00)\n";
}

void test_9_field_offset_invariants() {
    std::cout << "  Running Test 9: Field Offset Invariants... ";
    static_assert(offsetof(TraceEvent, sequence_number)    == 0,  "Offset mismatch");
    static_assert(offsetof(TraceEvent, virtual_time)       == 8,  "Offset mismatch");
    static_assert(offsetof(TraceEvent, pc)                 == 16, "Offset mismatch");
    static_assert(offsetof(TraceEvent, address)            == 24, "Offset mismatch");
    static_assert(offsetof(TraceEvent, data)               == 32, "Offset mismatch");
    static_assert(offsetof(TraceEvent, opcode_or_syndrome) == 40, "Offset mismatch");
    static_assert(offsetof(TraceEvent, flags)              == 44, "Offset mismatch");
    static_assert(offsetof(TraceEvent, event_type)         == 48, "Offset mismatch");
    static_assert(offsetof(TraceEvent, core_id)            == 50, "Offset mismatch");
    static_assert(offsetof(TraceEvent, exception_level)    == 52, "Offset mismatch");
    static_assert(offsetof(TraceEvent, address_space)      == 53, "Offset mismatch");
    static_assert(offsetof(TraceEvent, access_size)        == 54, "Offset mismatch");
    static_assert(offsetof(TraceEvent, reserved)           == 56, "Offset mismatch");

    assert(offsetof(TraceEvent, sequence_number)    == 0);
    assert(offsetof(TraceEvent, virtual_time)       == 8);
    assert(offsetof(TraceEvent, pc)                 == 16);
    assert(offsetof(TraceEvent, address)            == 24);
    assert(offsetof(TraceEvent, data)               == 32);
    assert(offsetof(TraceEvent, opcode_or_syndrome) == 40);
    assert(offsetof(TraceEvent, flags)              == 44);
    assert(offsetof(TraceEvent, event_type)         == 48);
    assert(offsetof(TraceEvent, core_id)            == 50);
    assert(offsetof(TraceEvent, exception_level)    == 52);
    assert(offsetof(TraceEvent, address_space)      == 53);
    assert(offsetof(TraceEvent, access_size)        == 54);
    assert(offsetof(TraceEvent, reserved)           == 56);
    std::cout << "PASS (zero padding, exact offsets)\n";
}

void test_10_reserved_field_invariants() {
    std::cout << "  Running Test 10: Reserved Field Invariants... ";
    TraceEvent ev{};
    assert(sizeof(ev.reserved) == 8);
    assert(ev.reserved == 0);

    // Verify copying preserves reserved
    TraceEvent ev2 = ev;
    assert(ev2.reserved == 0);
    std::cout << "PASS (8 reserved bytes verified)\n";
}

void test_11_construction_and_copying() {
    std::cout << "  Running Test 11: Construction and Copying... ";
    TraceEvent src{};
    src.sequence_number = 12345678ULL;
    src.virtual_time = 98765432ULL;
    src.pc = 0xFFFF000010002000ULL;
    src.address = 0xFFFF000080004000ULL;
    src.data = 0xDEADBEEFCAFEBABEULL;
    src.opcode_or_syndrome = 0xD503201F; // NOP
    src.flags = static_cast<std::uint32_t>(TraceEventFlags::HasVirtualAddress | TraceEventFlags::HasData);
    src.event_type = static_cast<std::uint16_t>(TraceEventType::InstructionRetired);
    src.core_id = 3;
    src.exception_level = static_cast<std::uint8_t>(TracePrivilegeLevel::EL1);
    src.address_space = static_cast<std::uint8_t>(TraceAddressSpace::Virtual);
    src.access_size = 4;
    src.reserved = 0;

    // Test memcpy
    TraceEvent dest_memcpy{};
    std::memcpy(&dest_memcpy, &src, sizeof(TraceEvent));
    assert(dest_memcpy == src);

    // Test copy constructor
    TraceEvent dest_copy(src);
    assert(dest_copy == src);

    // Test copy assignment
    TraceEvent dest_assign{};
    dest_assign = src;
    assert(dest_assign == src);

    // Test move constructor
    TraceEvent dest_move(std::move(src));
    assert(dest_move == dest_assign);

    std::cout << "PASS\n";
}

void test_12_assignment_and_equality() {
    std::cout << "  Running Test 12: Assignment and Equality... ";
    TraceEvent a{};
    TraceEvent b{};
    assert(a == b);
    assert(!(a != b));

    a.sequence_number = 1;
    assert(a != b);
    b.sequence_number = 1;
    assert(a == b);

    a.virtual_time = 100;
    assert(a != b);
    b.virtual_time = 100;
    assert(a == b);

    a.pc = 0x1000;
    assert(a != b);
    b.pc = 0x1000;
    assert(a == b);

    a.address = 0x2000;
    assert(a != b);
    b.address = 0x2000;
    assert(a == b);

    a.data = 0x3000;
    assert(a != b);
    b.data = 0x3000;
    assert(a == b);

    a.opcode_or_syndrome = 0x4000;
    assert(a != b);
    b.opcode_or_syndrome = 0x4000;
    assert(a == b);

    a.flags = 0x5;
    assert(a != b);
    b.flags = 0x5;
    assert(a == b);

    a.event_type = 2;
    assert(a != b);
    b.event_type = 2;
    assert(a == b);

    a.core_id = 1;
    assert(a != b);
    b.core_id = 1;
    assert(a == b);

    a.exception_level = 2;
    assert(a != b);
    b.exception_level = 2;
    assert(a == b);

    a.address_space = 1;
    assert(a != b);
    b.address_space = 1;
    assert(a == b);

    a.access_size = 8;
    assert(a != b);
    b.access_size = 8;
    assert(a == b);

    a.reserved = 0x1234;
    assert(a != b);
    b.reserved = 0x1234;
    assert(a == b);

    std::cout << "PASS\n";
}

void test_13_distinct_event_types() {
    std::cout << "  Running Test 13: Distinct Event Types... ";
    std::set<std::uint16_t> seen;
    const TraceEventType types[] = {
        TraceEventType::Invalid,
        TraceEventType::InstructionRetired,
        TraceEventType::BranchTaken,
        TraceEventType::ExceptionEntered,
        TraceEventType::ExceptionReturned,
        TraceEventType::InterruptDelivered,
        TraceEventType::SystemRegisterAccess,
        TraceEventType::MemoryRead,
        TraceEventType::MemoryWrite,
        TraceEventType::DebugEvent,
        TraceEventType::CPUStarted,
        TraceEventType::CPUStopped,
        TraceEventType::ContextChanged,
        TraceEventType::TimerEvent,
        TraceEventType::DroppedMarker,
        TraceEventType::TraceStart,
        TraceEventType::TraceEnd
    };

    for (auto t : types) {
        auto val = static_cast<std::uint16_t>(t);
        assert(seen.find(val) == seen.end() && "Duplicate event type value detected!");
        seen.insert(val);
    }
    std::cout << "PASS (17 distinct identifiers)\n";
}

void test_14_invalid_event_representation() {
    std::cout << "  Running Test 14: Invalid Event Representation... ";
    TraceEvent ev{};
    assert(ev.get_event_type() == TraceEventType::Invalid);
    assert(static_cast<std::uint16_t>(TraceEventType::Invalid) == 0);
    assert(to_string(TraceEventType::Invalid) == "Invalid");
    std::cout << "PASS (Invalid is 0 sentinel)\n";
}

void test_15_schema_version_availability() {
    std::cout << "  Running Test 15: Schema Version Availability... ";
    assert(TRACE_SCHEMA_VERSION_MAJOR == 1);
    assert(TRACE_SCHEMA_VERSION_MINOR == 0);
    assert(TRACE_SCHEMA_VERSION == 0x00010000u);
    std::cout << "PASS (Version 1.0 = 0x00010000)\n";
}

void test_16_strongly_typed_accessors() {
    std::cout << "  Running Test 16: Strongly Typed Accessors... ";
    TraceEvent ev{};
    ev.set_event_type(TraceEventType::MemoryRead);
    assert(ev.get_event_type() == TraceEventType::MemoryRead);

    ev.set_privilege_level(TracePrivilegeLevel::EL2);
    assert(ev.get_privilege_level() == TracePrivilegeLevel::EL2);

    ev.set_address_space(TraceAddressSpace::VirtualAndPhysical);
    assert(ev.get_address_space() == TraceAddressSpace::VirtualAndPhysical);

    ev.set_flags(TraceEventFlags::HasVirtualAddress);
    assert(ev.has_flag(TraceEventFlags::HasVirtualAddress));
    assert(!ev.has_flag(TraceEventFlags::HasData));

    ev.add_flag(TraceEventFlags::HasData);
    assert(ev.has_flag(TraceEventFlags::HasVirtualAddress));
    assert(ev.has_flag(TraceEventFlags::HasData));

    ev.clear_flag(TraceEventFlags::HasVirtualAddress);
    assert(!ev.has_flag(TraceEventFlags::HasVirtualAddress));
    assert(ev.has_flag(TraceEventFlags::HasData));

    std::cout << "PASS\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Phase 52 Stage 2: Trace Core Data Structures & Schema Tests ===\n\n";

    try {
        test_1_event_size();
        test_2_event_alignment();
        test_3_standard_layout();
        test_4_trivially_copyable();
        test_5_enum_widths();
        test_6_stable_event_identifiers();
        test_7_flag_bit_positions();
        test_8_default_zero_initialization();
        test_9_field_offset_invariants();
        test_10_reserved_field_invariants();
        test_11_construction_and_copying();
        test_12_assignment_and_equality();
        test_13_distinct_event_types();
        test_14_invalid_event_representation();
        test_15_schema_version_availability();
        test_16_strongly_typed_accessors();

        std::cout << "\n=== All 16 Phase 52 Stage 2 Tests PASSED Successfully! ===\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n=== Test FAILED with exception: " << e.what() << " ===\n";
        return 1;
    }
}
