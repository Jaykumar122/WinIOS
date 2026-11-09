#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/pmu.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "replay/replay_engine.h"
#include "sndl/sndl_recorder.h"
#include "sndl/sndl_source_hooks.h"
#include "snapshot/snapshot_serializer.h"
#include "snapshot/snapshot_manager.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kIterations = 1000000;
constexpr std::size_t kWarmups = 2;
constexpr std::size_t kRepetitions = 9;
constexpr std::uint32_t kCycleBit = 1u << 31u;
volatile std::uint64_t g_sink = 0;

void configure(winios::PMU& pmu, int mode) {
    pmu.reset();
    if (mode == 0) return;
    pmu.write_pmcr(winios::kPMCREnable);
    if (mode >= 2) pmu.write_pmcntenset(kCycleBit);
    if (mode >= 3) {
        pmu.write_pmcntenset(1u);
        pmu.write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED);
    }
    if (mode >= 4) {
        for (std::size_t i = 1; i < winios::kNumEventCounters; ++i) {
            pmu.write_pmcntenset(1u << i);
            pmu.write_pmevtyper(i, i == 1 ? winios::ARMV8_EVENT_BR_IMMED_RETIRED :
                                      i == 2 ? winios::ARMV8_EVENT_MEM_ACCESS :
                                      i == 3 ? winios::ARMV8_EVENT_L1D_CACHE_ACCESS :
                                      i == 4 ? winios::ARMV8_EVENT_L1D_CACHE_WRITE :
                                               winios::ARMV8_EVENT_EXC_TAKEN);
        }
    }
    if (mode == 5) {
        pmu.write_pmevcntr(0, 0xFFFFFFFFu);
        pmu.write_pmintenset(1u);
    }
}

std::uint64_t workload(int mode) {
    CPU cpu;
    Memory memory(0x500000);
    cpu.set_architectural_exceptions(false);
    cpu.set_current_el(1);
    configure(cpu.get_pmu(), mode);
    for (std::size_t i = 0; i < kIterations; ++i) memory.write_u32(0x1000 + i * 4, 0xD503201Fu);
    cpu.set_program_counter(0x1000);
    for (std::size_t i = 0; i < kIterations; ++i) cpu.step(memory);
    g_sink ^= cpu.get_pmu().read_pmccntr() ^ cpu.get_pmu().read_pmevcntr(0);
    return g_sink;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double run_mode(int mode) {
    std::vector<double> samples;
    for (std::size_t rep = 0; rep < kWarmups + kRepetitions; ++rep) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = workload(mode);
        const auto end = std::chrono::steady_clock::now();
        if (result == 0xFFFFFFFFFFFFFFFFULL) std::cerr << "";
        if (rep >= kWarmups) {
            samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }
    }
    return median(samples);
}

double snapshot_roundtrip() {
    winios::PMU pmu;
    configure(pmu, 4);
    std::vector<double> samples;
    for (std::size_t rep = 0; rep < kWarmups + kRepetitions; ++rep) {
        const auto start = std::chrono::steady_clock::now();
        auto data = winios::snapshot::serialize_pmu_state(pmu, 0);
        winios::PMU restored;
        (void)winios::snapshot::deserialize_pmu_state(data.data(), data.size(), restored);
        const auto end = std::chrono::steady_clock::now();
        if (rep >= kWarmups) {
            samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }

    }
    return median(samples);
}

double replay_roundtrip() {
    const std::filesystem::path snapshot_path = "pmu_benchmark.wsnap";
    const std::filesystem::path recording_path = "pmu_benchmark.sndl";
    CPU cpu;
    Memory memory(0x10000);
    winios::VirtualTime time;
    winios::GICv3 gic;
    cpu.bind_virtual_time(&time, 0);
    cpu.set_architectural_exceptions(false);
    cpu.set_current_el(1);
    cpu.get_pmu().write_pmcr(winios::kPMCREnable);
    cpu.get_pmu().write_pmcntenset(kCycleBit | 1u);
    cpu.get_pmu().write_pmevtyper(0, winios::ARMV8_EVENT_INST_RETIRED);
    cpu.set_program_counter(0);
    for (std::size_t i = 0; i < 12; ++i) memory.write_u32(i * 4, 0xD503201Fu);
    winios::snapshot::SnapshotRestoreContext context{};
    context.primary_cpu = &cpu;
    context.memory = &memory;
    context.virtual_time = &time;
    context.gic = &gic;
    (void)winios::snapshot::SnapshotManager::capture_to_file(context, snapshot_path);
    winios::sndl::SndlRecorder recorder;
    (void)recorder.start_recording(recording_path, 1, 0);
    (void)recorder.stop_recording(12);
    std::vector<double> samples;
    for (std::size_t rep = 0; rep < kWarmups + kRepetitions; ++rep) {
        winios::replay::ReplayConfig config{};
        config.max_instructions = 12;
        winios::replay::ReplayExecutionStats stats{};
        const auto start = std::chrono::steady_clock::now();
        (void)winios::replay::ReplayEngine::run(snapshot_path, recording_path, config, stats);
        const auto end = std::chrono::steady_clock::now();
        if (rep >= kWarmups) {
            samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }
    }
    std::filesystem::remove(snapshot_path);
    std::filesystem::remove(recording_path);
    return median(samples);
}

} // namespace

int main() {
    const double disabled = run_mode(0);
    const std::vector<std::string> names{
        "pmu_disabled", "pmu_enabled_no_events", "cycle_only",
        "one_event", "all_six_events", "overflow_interrupt"};
    const std::vector<int> modes{0, 1, 2, 3, 4, 5};
    std::cout << "PMU benchmark; build=Release/Debug selected by executable; iterations="
              << kIterations << "; warmups=" << kWarmups << "; repetitions=" << kRepetitions
              << "\n";
    std::cout << "configuration,median_ms,overhead_percent,mips\n";
    for (std::size_t i = 0; i < modes.size(); ++i) {
        const double value = run_mode(modes[i]);
        const double overhead = disabled > 0.0 ? ((value / disabled) - 1.0) * 100.0 : 0.0;
        const double mips = static_cast<double>(kIterations) / (value * 1000.0);
        std::cout << names[i] << "," << value << "," << overhead << "," << mips << "\n";
    }
    const double snapshot = snapshot_roundtrip();
    std::cout << "snapshot_restore," << snapshot << ",n/a,n/a\n";
    std::cout << "recording_replay," << replay_roundtrip() << ",n/a,n/a\n";
    return 0;
}
