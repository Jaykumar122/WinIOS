#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "cpu/cpu.h"
#include "cpu/smp_scheduler.h"
#include "sndl/sndl_v2_format.h"
#include "sndl/sndl_v2_recorder.h"
#include "sndl/sndl_v2_types.h"
#include "sndl/sndl_v2_validator.h"
#include "sndl/sndl_v2_writer.h"
#include "trace/trace_serializer.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

using namespace winios;
using namespace winios::sndl;

int main() {
    std::cout << "===============================================================\n";
    std::cout << "  Phase 52, Stage 25: SNDL v2 Hardening Benchmark Suite        \n";
    std::cout << "===============================================================\n\n";

    const auto temp_dir = std::filesystem::temp_directory_path() / "winios_stage25_bench";
    std::filesystem::create_directories(temp_dir);

    // --- Benchmark 1: SndlV2Record Direct Encode & Decode Throughput ---
    {
        std::cout << "--- Benchmark 1: Optimized Serialization Throughput (100,000 Records) ---\n";
        constexpr std::size_t kCount = 100000;
        std::vector<SndlV2Record> records(kCount);
        for (std::size_t i = 0; i < kCount; ++i) {
            records[i].global_event_sequence = i + 1;
            records[i].global_retirement_sequence = i;
            records[i].core_retirement_count = i;
            records[i].pc = 0x80000000 + i * 4;
            records[i].core_id = static_cast<std::uint32_t>(i % 4);
            records[i].event_type = SndlV2EventType::QuantumStart;
            records[i].boundary_type = SndlV2BoundaryType::SchedulerBoundary;
            records[i].payload1 = 1000;
        }

        std::vector<std::uint8_t> buffer(kCount * kWSndlV2RecordSize);

        const auto enc_t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < kCount; ++i) {
            encode_sndl_v2_record(records[i], buffer.data() + i * kWSndlV2RecordSize);
        }
        const auto enc_t1 = std::chrono::high_resolution_clock::now();
        const double enc_ms = std::chrono::duration<double, std::milli>(enc_t1 - enc_t0).count();
        const double enc_mrec = (kCount / (enc_ms / 1000.0)) / 1e6;
        const double enc_mb = (buffer.size() / (enc_ms / 1000.0)) / (1024.0 * 1024.0);

        std::vector<SndlV2Record> decoded(kCount);
        const auto dec_t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < kCount; ++i) {
            const auto res = decode_sndl_v2_record(buffer.data() + i * kWSndlV2RecordSize, kWSndlV2RecordSize, decoded[i]);
            assert(res.is_success());
        }
        const auto dec_t1 = std::chrono::high_resolution_clock::now();
        const double dec_ms = std::chrono::duration<double, std::milli>(dec_t1 - dec_t0).count();
        const double dec_mrec = (kCount / (dec_ms / 1000.0)) / 1e6;
        const double dec_mb = (buffer.size() / (dec_ms / 1000.0)) / (1024.0 * 1024.0);

        std::cout << "  Encode: " << enc_ms << " ms (" << enc_mrec << " M records/sec, " << enc_mb << " MB/sec)\n";
        std::cout << "  Decode: " << dec_ms << " ms (" << dec_mrec << " M records/sec, " << dec_mb << " MB/sec)\n\n";
    }

    // --- Benchmark 2: Block File Writing & Streaming Validation ---
    {
        std::cout << "--- Benchmark 2: Block File I/O and Streaming Validation ---\n";
        constexpr std::size_t kCount = 50000;
        const auto bench_file = temp_dir / "bench_io.sndl";

        WSndlV2Header hdr{};
        hdr.core_count = 4;
        hdr.initial_global_sequence = 1;

        SndlV2Writer writer(SndlV2WriterConfig{512, 100000});
        const auto open_res = writer.open(bench_file, hdr);
        assert(open_res.is_success());

        const auto wr_t0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < kCount; ++i) {
            SndlV2Record r{};
            r.global_event_sequence = i + 1;
            r.global_retirement_sequence = i;
            r.core_retirement_count = i / 4;
            r.core_id = static_cast<std::uint32_t>(i % 4);
            r.event_type = SndlV2EventType::CoreSelected;
            r.boundary_type = SndlV2BoundaryType::SchedulerBoundary;
            r.payload1 = i;
            const auto wr_res = writer.write_record(r);
            assert(wr_res.is_success());
        }
        const auto fin_res = writer.finalize(SndlV2CompletionStatus::Completed);
        assert(fin_res.is_success());
        const auto wr_t1 = std::chrono::high_resolution_clock::now();
        const double wr_ms = std::chrono::duration<double, std::milli>(wr_t1 - wr_t0).count();

        const auto file_size = std::filesystem::file_size(bench_file);
        const double wr_mb = (file_size / (wr_ms / 1000.0)) / (1024.0 * 1024.0);
        std::cout << "  Write " << kCount << " events to disk: " << wr_ms << " ms ("
                  << (file_size / (1024.0 * 1024.0)) << " MiB, " << wr_mb << " MB/sec)\n";

        const auto val_t0 = std::chrono::high_resolution_clock::now();
        const auto val_rep = SndlV2Validator::validate_file(bench_file);
        const auto val_t1 = std::chrono::high_resolution_clock::now();
        const double val_ms = std::chrono::duration<double, std::milli>(val_t1 - val_t0).count();
        const double val_mrec = (kCount / (val_ms / 1000.0)) / 1e6;
        const double val_mb = (file_size / (val_ms / 1000.0)) / (1024.0 * 1024.0);

        assert(val_rep.is_valid);
        assert(val_rep.total_events == kCount);
        std::cout << "  Streaming Validate: " << val_ms << " ms ("
                  << val_mrec << " M events/sec, " << val_mb << " MB/sec)\n\n";
    }

    // --- Benchmark 3: Scheduler Execution Overhead & Canonical Parity ---
    {
        std::cout << "--- Benchmark 3: Scheduler Execution Overhead & Canonical Parity ---\n";
        auto run_test = [&](std::size_t core_count, bool record) -> std::pair<double, std::uint64_t> {
            auto mem = std::make_shared<SmpSharedMemory>(1024 * 1024);
            SmpScheduler sched(mem, core_count, 100);
            for (std::size_t c = 0; c < core_count; ++c) {
                auto cpu = std::make_shared<CPU>();
                sched.register_core(c, cpu, CoreLifecycleState::Runnable);
            }

            for (std::size_t c = 0; c < core_count; ++c) {
                const std::uint64_t code_addr = c * 0x1000;
                mem->raw_memory().write_u32(code_addr + 0, 0xD2800020u | (static_cast<std::uint32_t>(c) << 5)); // MOV X<c>, #1
                mem->raw_memory().write_u32(code_addr + 4, 0xD2800041u);             // MOV X1, #2
                mem->raw_memory().write_u32(code_addr + 8, 0x14000000u);             // B . (loop)
                sched.get_slot(c).cpu->set_program_counter(code_addr);
            }

            std::unique_ptr<SndlV2Recorder> rec;
            const auto rec_path = temp_dir / ("bench_sched_" + std::to_string(core_count) + ".sndl");
            if (record) {
                rec = std::make_unique<SndlV2Recorder>(SndlV2RecorderOptions{static_cast<std::uint16_t>(core_count), 1024 * 1024});
                assert(rec->start_recording(rec_path).is_success());
                sched.attach_sndl_recorder(rec.get());
            }

            constexpr int kSteps = 500;
            const auto t0 = std::chrono::high_resolution_clock::now();
            for (int s = 0; s < kSteps; ++s) {
                sched.step_quantum();
            }
            const auto t1 = std::chrono::high_resolution_clock::now();

            if (record) {
                assert(rec->stop_recording(SndlV2CompletionStatus::Completed).is_success());
            }

            const double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            return {elapsed_ms, sched.compute_canonical_multicore_hash()};
        };

        for (std::size_t cores : {2, 4, 8}) {
            const auto [base_ms, base_hash] = run_test(cores, false);
            const auto [rec_ms, rec_hash] = run_test(cores, true);
            const double overhead = ((rec_ms - base_ms) / base_ms) * 100.0;
            const bool match = (base_hash == rec_hash);

            std::cout << "  [" << cores << " Cores] Base: " << base_ms << " ms | Rec: "
                      << rec_ms << " ms | Overhead: " << overhead << "% | Hash Parity: "
                      << (match ? "MATCH" : "MISMATCH") << "\n";
            assert(match);
        }
    }

    { std::error_code ec; std::filesystem::remove_all(temp_dir, ec); }

    std::cout << "\n===============================================================\n";
    std::cout << "  STAGE 25 BENCHMARK SUITE COMPLETE                            \n";
    std::cout << "===============================================================\n";
    return 0;
}
