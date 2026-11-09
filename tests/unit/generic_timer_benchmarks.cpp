#include "cpu/cpu.h"
#include "memory/memory.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr std::size_t kInstructions = 100000;
constexpr std::size_t kWarmups = 2;
constexpr std::size_t kRepetitions = 5;
constexpr std::uint32_t kNop = 0xD503201Fu;
volatile std::uint64_t sink = 0;

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double run(bool counter_reads, bool physical, bool virtual_timer) {
    std::vector<double> samples;
    for (std::size_t rep = 0; rep < kWarmups + kRepetitions; ++rep) {
        CPU cpu;
        Memory memory(0x80000);
        cpu.set_architectural_exceptions(false);
        cpu.set_program_counter(0);
        if (physical) cpu.set_phys_timer(winios::TimerState{true, false, false, kInstructions + 1});
        if (virtual_timer) cpu.set_virt_timer(winios::TimerState{true, false, false, kInstructions + 1});
        for (std::size_t i = 0; i < kInstructions; ++i) {
            memory.write_u32(i * 4, counter_reads ? 0xD53BE040u : kNop);
        }
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < kInstructions; ++i) cpu.step(memory);
        const auto end = std::chrono::steady_clock::now();
        sink ^= cpu.get_guest_counter();
        if (rep >= kWarmups) {
            samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }
    }
    return median(samples);
}
}

int main() {
    const double baseline = run(false, false, false);
    std::cout << "Generic Timer benchmark; instructions=" << kInstructions
              << "; warmups=" << kWarmups << "; repetitions=" << kRepetitions << "\n";
    std::cout << "configuration,median_ms,overhead_percent\n";
    for (const auto& entry : {std::pair{"disabled", std::array{false, false, false}},
                              std::pair{"counter_reads", std::array{true, false, false}},
                              std::pair{"physical_timer", std::array{false, true, false}},
                              std::pair{"virtual_timer", std::array{false, false, true}},
                              std::pair{"both_timers", std::array{false, true, true}}}) {
        const double value = run(entry.second[0], entry.second[1], entry.second[2]);
        std::cout << entry.first << "," << value << ","
                  << ((value / baseline) - 1.0) * 100.0 << "\n";
    }
}
