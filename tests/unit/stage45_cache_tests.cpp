#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "memory/memory.h"

namespace {

constexpr std::uint32_t kMrsCtrX0 = 0xD53B0020u;
constexpr std::uint32_t kMrsDczidX1 = 0xD53B00E1u;
constexpr std::uint32_t kDcZvaX0 = 0xD5037420u;
constexpr std::uint32_t kDcCvacX0 = 0xD5037A20u;
constexpr std::uint32_t kDcCvauX0 = 0xD5037B20u;
constexpr std::uint32_t kDcCivacX0 = 0xD5037E20u;
constexpr std::uint32_t kIcIvauX0 = 0xD5037520u;
constexpr std::uint32_t kIcIallu = 0xD500751Fu;
constexpr std::uint32_t kDsb = 0xD5033F9Fu;
constexpr std::uint32_t kIsb = 0xD5033FDFu;
constexpr std::uint32_t kNop = 0xD503201Fu;

struct ReferenceCache {
    static constexpr std::size_t line_bytes = 64;

    static std::uint64_t align(std::uint64_t address) {
        return address & ~(static_cast<std::uint64_t>(line_bytes) - 1u);
    }

    static void zero_block(Memory& memory, std::uint64_t address) {
        const auto base = static_cast<std::size_t>(align(address));
        for (std::size_t offset = 0; offset < line_bytes; offset += 8) {
            memory.write_u64(base + offset, 0);
        }
    }
};

void run(CPU& cpu, Memory& memory, std::uint32_t instruction) {
    memory.write_u32(cpu.get_program_counter(), instruction);
    cpu.step(memory);
}

void test_registers() {
    CPU cpu;
    Memory memory(0x400);
    run(cpu, memory, kMrsCtrX0);
    run(cpu, memory, kMrsDczidX1);
    assert(cpu.read_register(0) == 0x4004);
    assert(cpu.read_register(1) == 0x4);
}

void test_zva_and_fault_safety() {
    CPU cpu;
    Memory memory(0x200);
    const std::uint64_t address = 0x80;
    for (std::size_t i = 0; i < ReferenceCache::line_bytes; ++i) {
        memory.write_u8(static_cast<std::size_t>(address + i), 0xA5);
    }
    cpu.write_register(0, address + 7);
    run(cpu, memory, kDcZvaX0);
    for (std::size_t i = 0; i < ReferenceCache::line_bytes; ++i) {
        assert(memory.read_u8(static_cast<std::size_t>(address + i)) == 0);
    }

    CPU fault_cpu;
    Memory small(0x80);
    for (std::size_t i = 0; i < small.size(); ++i) {
        small.write_u8(i, 0x5A);
    }
    fault_cpu.write_register(0, 0xBF);
    bool faulted = false;
    try {
        run(fault_cpu, small, kDcZvaX0);
    } catch (const std::out_of_range&) {
        faulted = true;
    }
    assert(faulted);
    for (std::size_t i = 4; i < small.size(); ++i) {
        assert(small.read_u8(i) == 0x5A);
    }
}

void test_maintenance_and_barriers() {
    CPU cpu;
    Memory memory(0x400);
    cpu.write_register(0, 0x100);
    memory.write_u32(0x100, kNop);
    run(cpu, memory, kDcCvacX0);
    run(cpu, memory, kDcCvauX0);
    run(cpu, memory, kDcCivacX0);
    run(cpu, memory, kIcIvauX0);
    run(cpu, memory, kIcIallu);
    run(cpu, memory, kDsb);
    run(cpu, memory, kIsb);
    assert(cpu.get_program_counter() == 28);
}

void test_self_modifying_code_and_cross_core_visibility() {
    Memory memory(0x400);
    CPU core0;
    CPU core1;
    core0.set_program_counter(0x200);
    core1.set_program_counter(0x200);
    memory.write_u32(0x200, kNop);
    core0.step(memory);
    memory.write_u32(0x200, 0xD2800020u); // MOV X0, #1
    core0.write_register(0, 0x200);
    run(core0, memory, kDcCvacX0);
    run(core0, memory, kDsb);
    run(core0, memory, kIcIvauX0);
    run(core0, memory, kIsb);
    core1.step(memory);
    assert(core1.read_register(0) == 1);
}

void test_independent_reference() {
    Memory memory(0x400);
    for (std::size_t i = 0; i < ReferenceCache::line_bytes; ++i) {
        memory.write_u8(0x140 + i, 0xCC);
    }
    ReferenceCache::zero_block(memory, 0x147);
    for (std::size_t i = 0; i < ReferenceCache::line_bytes; ++i) {
        assert(memory.read_u8(0x140 + i) == 0);
    }
}

} // namespace

int main() {
    test_registers();
    test_zva_and_fault_safety();
    test_maintenance_and_barriers();
    test_self_modifying_code_and_cross_core_visibility();
    test_independent_reference();
    std::cout << "Stage 45 cache register, maintenance, barrier, self-modifying-code, SMP, and reference tests passed\n";
}
