#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace winios::replay {

// -----------------------------------------------------------------------------
// ReplayWorkloadSpec: Deterministic 100-instruction counter-read loop workload
//
// Program layout (loop_instructions = 100 by default):
//   [0]              MRS X0, CNTPCT_EL0
//   [1 .. N-2]       NOP
//   [N-1]            B <loop start>
//
// A CounterReadResult record is emitted every counter_event_stride retired
// instructions, i.e. exactly once per loop iteration. The default matrix is
// 100 instructions per event: 100k -> 1k, 1M -> 10k, 10M -> 100k,
// 50M -> 500k, 100M -> 1M records.
// -----------------------------------------------------------------------------
struct ReplayWorkloadSpec {
    std::uint64_t instructions{1000000};
    std::uint64_t loop_instructions{100};
    std::uint64_t counter_event_stride{100};
    std::uint64_t memory_bytes{16ULL * 1024ULL * 1024ULL};
    std::uint16_t core_id{0};
    std::uint32_t counter_reg_key{0x5F01}; // MRS encoding of CNTPCT_EL0
    std::uint64_t counter_frequency{24000000};
    bool include_counter_events{true};
    bool pattern_memory{true};
    std::uint64_t pattern_memory_bytes{1024ULL * 1024ULL}; // 0 = entire guest RAM
    std::string description{"Stage 21 deterministic counter-loop workload"};
    std::filesystem::path wsnap_path{"stage21_workload.wsnap"};
    std::filesystem::path sndl_path{"stage21_workload.sndl"};
};

struct ReplayWorkloadResult {
    bool success{false};
    std::string message;
    std::uint64_t total_instructions{0};
    std::uint64_t total_records{0};
    std::uint64_t wsnap_bytes{0};
    std::uint64_t sndl_bytes{0};
    std::uint64_t memory_bytes{0};
    std::uint64_t base_snapshot_hash{0};
};

// Generates a fresh baseline .wsnap and matching .sndl in the requested paths.
// Existing files at those paths are overwritten.
[[nodiscard]] ReplayWorkloadResult generate_counter_loop_workload(
    const ReplayWorkloadSpec& spec
);

// Returns the exact AArch64 instruction encodings used by the workload.
[[nodiscard]] std::uint32_t workload_counter_read_instruction() noexcept;
[[nodiscard]] std::uint32_t workload_nop_instruction() noexcept;
[[nodiscard]] std::uint32_t workload_branch_instruction(
    std::uint64_t loop_instructions
) noexcept;

} // namespace winios::replay
