#include "cpu/cpu.h"
#include "cpu/pmu.h"
#include "cpu/smp_core_state.h"
#include "memory/memory.h"
#include "snapshot/snapshot_serializer.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

constexpr std::uint32_t kCycleBit = 1u << 31u;
constexpr std::uint32_t kEventMask = (1u << winios::kNumEventCounters) - 1u;

struct ReferenceModel {
    std::uint32_t pmcr = static_cast<std::uint32_t>(winios::kNumEventCounters)
                         << winios::kPMCRNumCountersShift;
    std::uint32_t enable = 0;
    std::uint32_t overflow = 0;
    std::uint32_t interrupt_enable = 0;
    std::uint32_t cycle_filter = 0;
    std::uint64_t cycle = 0;
    std::uint64_t prescaler = 0;
    std::array<std::uint32_t, winios::kNumEventCounters> values{};
    std::array<std::uint32_t, winios::kNumEventCounters> types{};
    std::array<std::uint32_t, winios::kNumEventCounters> filters{};

    static bool permitted(std::uint32_t filter, unsigned el) {
        if (el == 0) return (filter & winios::kFilterU) == 0;
        if (el == 1) return (filter & winios::kFilterP) == 0;
        if (el == 2) return (filter & winios::kFilterNSH) == 0;
        if (el == 3) return (filter & winios::kFilterM) == 0;
        return true;
    }

    void event(std::uint32_t id, unsigned el, std::uint32_t count = 1) {
        if ((pmcr & winios::kPMCREnable) == 0 || count == 0) return;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if ((enable & (1u << i)) == 0 || (types[i] & 0xFFFFu) != id ||
                !permitted(filters[i], el)) {
                continue;
            }
            const auto old = values[i];
            values[i] += count;
            if (values[i] < old) overflow |= 1u << i;
        }
    }

    void cycle_tick(unsigned el, std::uint64_t count = 1) {
        if ((pmcr & winios::kPMCREnable) == 0 || count == 0) return;
        if ((enable & kCycleBit) != 0 && permitted(cycle_filter, el)) {
            if ((pmcr & winios::kPMCRClockDivider) != 0) {
                prescaler += count;
                const auto ticks = prescaler / 64;
                prescaler %= 64;
                const auto old = cycle;
                cycle += ticks;
                if (ticks != 0 && (pmcr & winios::kPMCRLongCycles) != 0 && cycle < old) {
                    overflow |= kCycleBit;
                } else if (ticks != 0 && (pmcr & winios::kPMCRLongCycles) == 0 &&
                           ((old & 0xFFFFFFFFULL) + ticks > 0xFFFFFFFFULL)) {
                    overflow |= kCycleBit;
                }
            } else {
                const auto old = cycle;
                cycle += count;
                if ((pmcr & winios::kPMCRLongCycles) != 0 && cycle < old) {
                    overflow |= kCycleBit;
                } else if ((pmcr & winios::kPMCRLongCycles) == 0 &&
                           ((old & 0xFFFFFFFFULL) + count > 0xFFFFFFFFULL)) {
                    overflow |= kCycleBit;
                }
            }
        }
        event(winios::ARMV8_EVENT_CPU_CYCLES, el, static_cast<std::uint32_t>(count));
    }

    bool pending() const { return (overflow & interrupt_enable) != 0; }
};

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void compare(const winios::PMU& actual, const ReferenceModel& expected) {
    check(actual.read_pmcr() == expected.pmcr, "PMCR");
    check(actual.read_pmcntenset() == expected.enable, "enable mask");
    check(actual.read_pmovsset() == expected.overflow, "overflow mask");
    check(actual.read_pmintenset() == expected.interrupt_enable, "interrupt mask");
    check(actual.read_pmccntr() == expected.cycle, "cycle counter");
    check(actual.cycle_prescaler() == expected.prescaler, "cycle prescaler");
    for (std::size_t i = 0; i < winios::kNumEventCounters; ++i) {
        check(actual.read_pmevcntr(i) == expected.values[i], "event counter");
    }
    check(actual.has_pending_interrupt() == expected.pending(), "pending interrupt");
}

void test_reference_trace() {
    winios::PMU actual;
    ReferenceModel expected;
    const auto enable = kCycleBit | kEventMask;
    actual.write_pmcr(winios::kPMCREnable);
    expected.pmcr = winios::kPMCREnable |
                    (static_cast<std::uint32_t>(winios::kNumEventCounters)
                     << winios::kPMCRNumCountersShift);
    actual.write_pmcntenset(enable);
    expected.enable = enable;
    actual.write_pmintenset(1u);
    expected.interrupt_enable = 1u;
    for (std::size_t i = 0; i < winios::kNumEventCounters; ++i) {
        actual.write_pmevtyper(i, i == 0 ? winios::ARMV8_EVENT_INST_RETIRED :
                                  i == 1 ? winios::ARMV8_EVENT_BR_IMMED_RETIRED :
                                  i == 2 ? winios::ARMV8_EVENT_EXC_TAKEN :
                                  i == 3 ? winios::ARMV8_EVENT_MEM_ACCESS :
                                  i == 4 ? winios::ARMV8_EVENT_L1D_CACHE_WRITE :
                                           winios::ARMV8_EVENT_SW_INCR);
        expected.types[i] = actual.read_pmevtyper(i);
    }

    const std::array<std::pair<std::uint32_t, std::uint32_t>, 6> trace{{
        {winios::ARMV8_EVENT_INST_RETIRED, 7},
        {winios::ARMV8_EVENT_BR_IMMED_RETIRED, 3},
        {winios::ARMV8_EVENT_EXC_TAKEN, 2},
        {winios::ARMV8_EVENT_MEM_ACCESS, 5},
        {winios::ARMV8_EVENT_L1D_CACHE_WRITE, 4},
        {winios::ARMV8_EVENT_SW_INCR, 1},
    }};
    for (const auto& item : trace) {
        actual.on_event(item.first, 1, item.second);
        expected.event(item.first, 1, item.second);
    }
    actual.on_cycle(1, 37);
    expected.cycle_tick(1, 37);
    compare(actual, expected);

    actual.write_pmevcntr(0, 0xFFFFFFFFu);
    expected.values[0] = 0xFFFFFFFFu;
    actual.on_event(winios::ARMV8_EVENT_INST_RETIRED, 1);
    expected.event(winios::ARMV8_EVENT_INST_RETIRED, 1);
    compare(actual, expected);
    actual.write_pmovsclr(1u);
    expected.overflow &= ~1u;
    check(!actual.has_pending_interrupt() && !expected.pending(), "overflow clear");

    actual.write_pmselr(31);
    check(actual.read_pmxevcntr() == 0 && actual.read_pmxevtyper() == 0, "invalid selector");
}

void test_filter_and_divider() {
    winios::PMU actual;
    ReferenceModel expected;
    actual.write_pmcr(winios::kPMCREnable | winios::kPMCRClockDivider);
    expected.pmcr = winios::kPMCREnable | winios::kPMCRClockDivider |
                    (static_cast<std::uint32_t>(winios::kNumEventCounters)
                     << winios::kPMCRNumCountersShift);
    actual.write_pmcntenset(kCycleBit | 1u);
    expected.enable = kCycleBit | 1u;
    actual.write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED | winios::kFilterP);
    expected.types[0] = winios::ARMV8_EVENT_INST_RETIRED | winios::kFilterP;
    expected.filters[0] = expected.types[0];
    actual.write_pmccfiltr(winios::kFilterP);
    expected.cycle_filter = winios::kFilterP;
    actual.on_cycle(1, 63);
    expected.cycle_tick(1, 63);
    actual.on_cycle(1, 1);
    expected.cycle_tick(1, 1);
    actual.on_event(winios::ARMV8_EVENT_INST_RETIRED, 1);
    expected.event(winios::ARMV8_EVENT_INST_RETIRED, 1);
    compare(actual, expected);
    check(actual.read_pmccntr() == 0 && actual.read_pmevcntr(0) == 0, "EL1 filtering");
}

void test_direct_execution_and_snapshot() {
    CPU cpu;
    Memory memory(0x10000);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenset(kCycleBit | 1u);
    cpu.get_pmu().write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED);
    for (std::size_t i = 0; i < 16; ++i) memory.write_u32(0x1000 + i * 4, 0xD503201Fu);
    cpu.set_program_counter(0x1000);
    for (std::size_t i = 0; i < 16; ++i) cpu.step(memory);
    check(cpu.get_pmu().read_pmccntr() == 16, "direct cycle execution");
    check(cpu.get_pmu().read_pmevcntr(0) == 16, "direct retired execution");

    auto encoded = winios::snapshot::serialize_pmu_state(cpu.get_pmu(), 0);
    winios::PMU restored;
    check(winios::snapshot::deserialize_pmu_state(encoded.data(), encoded.size(), restored).success,
          "PMU snapshot restore");
    check(restored.read_pmccntr() == cpu.get_pmu().read_pmccntr() &&
          restored.read_pmevcntr(0) == cpu.get_pmu().read_pmevcntr(0),
          "snapshot values");

    winios::PMU replay;
    replay.write_pmcr(winios::kPMCREnable);
    replay.write_pmcntenset(kCycleBit | 1u);
    replay.write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED);
    for (std::size_t i = 0; i < 16; ++i) {
        replay.on_cycle(1);
        replay.on_event(winios::ARMV8_EVENT_INST_RETIRED, 1);
    }
    check(replay.read_pmccntr() == cpu.get_pmu().read_pmccntr() &&
          replay.read_pmevcntr(0) == cpu.get_pmu().read_pmevcntr(0),
          "replay trace parity");

    CPU core_a;
    CPU core_b;
    core_a.set_core_id(0);
    core_b.set_core_id(1);
    core_a.get_pmu().write_pmccntr(11);
    core_b.get_pmu().write_pmccntr(22);
    winios::SmpCoreState state_a;
    winios::SmpCoreState state_b;
    state_a.capture_from_cpu(core_a);
    state_b.capture_from_cpu(core_b);
    check(state_a.compute_canonical_hash() != state_b.compute_canonical_hash(),
          "per-core PMU hash isolation");
    CPU restored_core;
    state_a.apply_to_cpu(restored_core);
    winios::SmpCoreState restored_state;
    restored_state.capture_from_cpu(restored_core);
    check(state_a == restored_state, "per-core PMU state restoration");

    CPU irq_cpu;
    irq_cpu.set_architectural_exceptions(true);
    irq_cpu.set_current_el(1);
    irq_cpu.set_daif(0);
    irq_cpu.get_gic().write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4);
    irq_cpu.get_gic().write_icc_sre_el1(1, 0);
    irq_cpu.get_gic().write_icc_igrpen1(1, 0);
    irq_cpu.get_gic().write_icc_pmr(0xF0, 0);
    irq_cpu.get_gic().write_mmio(0x080B0080, 1u << 23, 4);
    irq_cpu.get_gic().write_mmio(0x080B0400 + 23, 0x30, 1);
    irq_cpu.get_gic().write_mmio(0x080B0100, 1u << 23, 4);
    irq_cpu.get_pmu().write_pmintenset(kCycleBit);
    irq_cpu.get_pmu().write_pmovsset(kCycleBit);
    irq_cpu.check_pending_interrupts();
    check(irq_cpu.get_gic().evaluate(1, 0, 0).has_interrupt, "PPI 23 delivery");
    irq_cpu.get_pmu().write_pmovsclr(kCycleBit);
    irq_cpu.check_pending_interrupts();
    check(!irq_cpu.get_gic().evaluate(1, 0, 0).has_interrupt, "PPI 23 clear");
}

} // namespace

int main() {
    test_reference_trace();
    test_filter_and_divider();
    test_direct_execution_and_snapshot();
    std::cout << "PMU independent reference validation: 3 suites passed\n";
    return 0;
}
