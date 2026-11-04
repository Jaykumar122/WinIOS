#include "sndl/sndl_v2_format.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_types.h"
#include "sndl/sndl_v2_validator.h"
#include "sndl/sndl_v2_writer.h"
#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

void benchmark_record_encoding_decoding() {
    std::cout << "\n--- Benchmark 1: SndlV2Record Encode & Decode Throughput ---\n";

    constexpr std::size_t kIterations = 50000;
    std::vector<winios::sndl::SndlV2Record> records(kIterations);
    for (std::size_t i = 0; i < kIterations; ++i) {
        records[i].global_event_sequence = i + 1;
        records[i].global_retirement_sequence = (i + 1) * 10;
        records[i].core_retirement_count = (i + 1) * 2;
        records[i].pc = 0x80000000ULL + i * 4;
        records[i].payload1 = 0x12345678ULL;
        records[i].payload2 = 0x9ABCDEF0ULL;
        records[i].core_id = static_cast<std::uint32_t>(i % 4);
        records[i].event_type = winios::sndl::SndlV2EventType::QuantumStart;
        records[i].boundary_type = winios::sndl::SndlV2BoundaryType::SchedulerBoundary;
    }

    std::vector<std::uint8_t> buffer(kIterations * winios::sndl::kWSndlV2RecordSize);

    // 1. Encoding
    const auto enc_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        winios::sndl::encode_sndl_v2_record(records[i], buffer.data() + i * winios::sndl::kWSndlV2RecordSize);
    }
    const auto enc_end = std::chrono::steady_clock::now();
    const double enc_ms = std::chrono::duration<double, std::milli>(enc_end - enc_start).count();
    const double enc_events_sec = (kIterations / (enc_ms / 1000.0));
    const double enc_mb_sec = (kIterations * 80.0 / (1024.0 * 1024.0)) / (enc_ms / 1000.0);

    std::cout << "  Encode: " << std::fixed << std::setprecision(2) << enc_ms << " ms ("
              << (enc_events_sec / 1e6) << " M records/sec, " << enc_mb_sec << " MB/sec)\n";

    // 2. Decoding
    winios::sndl::SndlV2Record decoded{};
    const auto dec_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kIterations; ++i) {
        (void)winios::sndl::decode_sndl_v2_record(buffer.data() + i * winios::sndl::kWSndlV2RecordSize,
                                                  winios::sndl::kWSndlV2RecordSize, decoded);
    }
    const auto dec_end = std::chrono::steady_clock::now();
    const double dec_ms = std::chrono::duration<double, std::milli>(dec_end - dec_start).count();
    const double dec_events_sec = (kIterations / (dec_ms / 1000.0));
    const double dec_mb_sec = (kIterations * 80.0 / (1024.0 * 1024.0)) / (dec_ms / 1000.0);

    std::cout << "  Decode: " << std::fixed << std::setprecision(2) << dec_ms << " ms ("
              << (dec_events_sec / 1e6) << " M records/sec, " << dec_mb_sec << " MB/sec)\n";
}

void benchmark_file_io_and_streaming_validation() {
    std::cout << "\n--- Benchmark 2: Block File I/O and Streaming Validation ---\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_sndl_v2_bm";
    std::filesystem::create_directories(temp_dir);
    const auto file_path = temp_dir / "benchmark.sndl";

    constexpr std::uint64_t kRecordCount = 25000;
    winios::sndl::SndlV2Writer writer(winios::sndl::SndlV2WriterConfig{512, kRecordCount * 2});
    winios::sndl::WSndlV2Header hdr{};
    hdr.core_count = 8;
    hdr.memory_capacity = 64 * 1024 * 1024;

    const auto write_start = std::chrono::steady_clock::now();
    assert(writer.open(file_path, hdr).is_success());

    for (std::uint64_t i = 1; i <= kRecordCount; ++i) {
        winios::sndl::SndlV2Record r{};
        r.global_event_sequence = i;
        r.global_retirement_sequence = i * 4;
        r.core_retirement_count = i / 2;
        r.pc = 0x80000000ULL + (i % 1000) * 4;
        r.core_id = static_cast<std::uint32_t>((i - 1) % 8);
        r.event_type = winios::sndl::SndlV2EventType::QuantumStart;
        r.boundary_type = winios::sndl::SndlV2BoundaryType::SchedulerBoundary;
        (void)writer.write_record(r);
    }
    assert(writer.finalize(winios::sndl::SndlV2CompletionStatus::Completed).is_success());
    const auto write_end = std::chrono::steady_clock::now();

    const double write_ms = std::chrono::duration<double, std::milli>(write_end - write_start).count();
    const auto file_size = std::filesystem::file_size(file_path);
    const double write_mb_sec = (file_size / (1024.0 * 1024.0)) / (write_ms / 1000.0);

    std::cout << "  Write " << kRecordCount << " events to disk: " << std::fixed << std::setprecision(2)
              << write_ms << " ms (" << (file_size / (1024.0 * 1024.0)) << " MiB, "
              << write_mb_sec << " MB/sec)\n";

    // Streaming validation
    const auto val_start = std::chrono::steady_clock::now();
    const auto val = winios::sndl::SndlV2Validator::validate_file(file_path);
    const auto val_end = std::chrono::steady_clock::now();

    assert(val.is_valid);
    const double val_ms = std::chrono::duration<double, std::milli>(val_end - val_start).count();
    const double val_events_sec = (kRecordCount / (val_ms / 1000.0));
    const double val_mb_sec = (file_size / (1024.0 * 1024.0)) / (val_ms / 1000.0);

    std::cout << "  Streaming Validate: " << std::fixed << std::setprecision(2) << val_ms << " ms ("
              << (val_events_sec / 1e6) << " M events/sec, " << val_mb_sec << " MB/sec)\n";

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
}

void benchmark_scheduler_recording_overhead() {
    std::cout << "\n--- Benchmark 3: Scheduler Execution Overhead (Recording vs Baseline) ---\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_sndl_v2_bm_sched";
    std::filesystem::create_directories(temp_dir);

    for (std::size_t num_cores : {2, 4, 8}) {
        const std::uint64_t kInstructions = 50000;

        // Baseline (unrecorded)
        auto mem_base = std::make_shared<winios::SmpSharedMemory>(1024 * 1024);
        winios::SmpScheduler sched_base(mem_base, num_cores, 100);
        for (std::size_t c = 0; c < num_cores; ++c) {
            auto cpu = std::make_shared<CPU>();
            const std::uint64_t code_addr = 0x1000 + c * 0x100;
            mem_base->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>(c) << 5));
            mem_base->raw_memory().write_u32(code_addr + 4, 0x91000400u);
            mem_base->raw_memory().write_u32(code_addr + 8, 0x14000000u);
            cpu->set_program_counter(code_addr);
            sched_base.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
        }

        const auto start_base = std::chrono::steady_clock::now();
        sched_base.run_deterministic(kInstructions);
        const auto end_base = std::chrono::steady_clock::now();
        const double base_ms = std::chrono::duration<double, std::milli>(end_base - start_base).count();
        const std::uint64_t hash_base = sched_base.compute_canonical_multicore_hash();

        // Recorded
        const auto sndl_path = temp_dir / ("sched_" + std::to_string(num_cores) + "cores.sndl");
        auto mem_rec = std::make_shared<winios::SmpSharedMemory>(1024 * 1024);
        winios::SmpScheduler sched_rec(mem_rec, num_cores, 100);
        for (std::size_t c = 0; c < num_cores; ++c) {
            auto cpu = std::make_shared<CPU>();
            const std::uint64_t code_addr = 0x1000 + c * 0x100;
            mem_rec->raw_memory().write_u32(code_addr, 0xD2800000u | (static_cast<std::uint32_t>(c) << 5));
            mem_rec->raw_memory().write_u32(code_addr + 4, 0x91000400u);
            mem_rec->raw_memory().write_u32(code_addr + 8, 0x14000000u);
            cpu->set_program_counter(code_addr);
            sched_rec.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
        }

        winios::sndl::SndlV2Recorder recorder(winios::sndl::SndlV2RecorderOptions{
            static_cast<std::uint16_t>(num_cores), 1024 * 1024, 0, 0, 0, 512
        });
        assert(recorder.start_recording(sndl_path).is_success());
        sched_rec.attach_sndl_recorder(&recorder);

        const auto start_rec = std::chrono::steady_clock::now();
        sched_rec.run_deterministic(kInstructions);
        const auto end_rec = std::chrono::steady_clock::now();
        const double rec_ms = std::chrono::duration<double, std::milli>(end_rec - start_rec).count();
        assert(recorder.stop_recording(winios::sndl::SndlV2CompletionStatus::Completed).is_success());

        const std::uint64_t hash_rec = sched_rec.compute_canonical_multicore_hash();
        assert(hash_base == hash_rec); // Zero divergence verified

        const double overhead_pct = ((rec_ms - base_ms) / base_ms) * 100.0;
        const double ns_per_inst_base = (base_ms * 1e6) / kInstructions;
        const double ns_per_inst_rec = (rec_ms * 1e6) / kInstructions;

        std::cout << "  [" << num_cores << " Cores] " << kInstructions << " instrs | Base: "
                  << std::fixed << std::setprecision(2) << base_ms << " ms (" << ns_per_inst_base << " ns/inst) | "
                  << "Rec: " << rec_ms << " ms (" << ns_per_inst_rec << " ns/inst) | "
                  << "Overhead: " << std::setprecision(1) << (overhead_pct < 0.0 ? 0.0 : overhead_pct) << "% | Hash Parity: MATCH\n";
    }

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }
}

} // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    std::cout << "===============================================================\n";
    std::cout << "  Phase 52, Stage 24: Multi-Core SNDL v2 Performance Benchmark \n";
    std::cout << "===============================================================\n";

    benchmark_record_encoding_decoding();
    benchmark_file_io_and_streaming_validation();
    benchmark_scheduler_recording_overhead();

    std::cout << "\n===============================================================\n";
    std::cout << "  BENCHMARK SUITE COMPLETE                                     \n";
    std::cout << "===============================================================\n";
    return 0;
}
