#include "replay/replay_workload.h"

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "sndl/sndl_event_encoder.h"
#include "sndl/sndl_format.h"
#include "sndl/sndl_writer.h"
#include "snapshot/snapshot_manager.h"

#include <algorithm>
#include <system_error>
#include <vector>

namespace winios::replay {

namespace {

constexpr std::uint32_t kMrsCntpctX0 = 0xD53BE020u; // MRS X0, CNTPCT_EL0
constexpr std::uint32_t kNop = 0xD503201Fu;          // NOP
constexpr std::uint32_t kBranchBase = 0x14000000u;   // B <imm26>

std::uint64_t file_size_or_zero(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0ULL : static_cast<std::uint64_t>(size);
}

} // namespace

std::uint32_t workload_counter_read_instruction() noexcept {
    return kMrsCntpctX0;
}

std::uint32_t workload_nop_instruction() noexcept {
    return kNop;
}

std::uint32_t workload_branch_instruction(std::uint64_t loop_instructions) noexcept {
    if (loop_instructions < 2) {
        return kBranchBase;
    }
    // Branch at index (N-1), i.e. byte offset (N-1)*4, targeting offset 0.
    // AArch64 B immediate is a signed word offset in bits [25:0].
    const std::int64_t word_offset = -static_cast<std::int64_t>(loop_instructions - 1);
    const std::uint32_t imm26 = static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu;
    return kBranchBase | imm26;
}

ReplayWorkloadResult generate_counter_loop_workload(const ReplayWorkloadSpec& spec) {
    ReplayWorkloadResult result{};
    result.total_instructions = spec.instructions;
    result.memory_bytes = spec.memory_bytes;

    if (spec.instructions == 0) {
        result.message = "instructions must be non-zero";
        return result;
    }
    if (spec.loop_instructions < 2 || spec.loop_instructions > 4096) {
        result.message = "loop_instructions must be in [2, 4096]";
        return result;
    }
    if (spec.instructions % spec.loop_instructions != 0) {
        result.message = "instructions must be an exact multiple of loop_instructions";
        return result;
    }
    if (spec.include_counter_events) {
        if (spec.counter_event_stride != spec.loop_instructions) {
            result.message = "counter_event_stride must equal loop_instructions for exact event placement";
            return result;
        }
        if (spec.instructions % spec.counter_event_stride != 0) {
            result.message = "instructions must be an exact multiple of counter_event_stride";
            return result;
        }
    }
    if (spec.memory_bytes < spec.loop_instructions * 4ULL) {
        result.message = "memory_bytes is too small for the workload loop";
        return result;
    }

    std::error_code ec;
    std::filesystem::remove(spec.wsnap_path, ec);
    ec.clear();
    std::filesystem::remove(spec.sndl_path, ec);

    CPU cpu;
    Memory memory(static_cast<std::size_t>(spec.memory_bytes));
    VirtualTime vt;
    GICv3 gic;

    cpu.reset();
    cpu.bind_virtual_time(&vt, spec.core_id);

    std::vector<std::uint32_t> program(static_cast<std::size_t>(spec.loop_instructions), kNop);
    program.front() = kMrsCntpctX0;
    program.back() = workload_branch_instruction(spec.loop_instructions);
    for (std::size_t i = 0; i < program.size(); ++i) {
        memory.write_u32(i * 4, program[i]);
    }

    // A non-zero region makes the full-memory hash sensitive to corruption and
    // keeps the workload representative of a populated guest image.
    if (spec.pattern_memory) {
        const std::uint64_t requested_pattern_bytes =
            spec.pattern_memory_bytes == 0 ? spec.memory_bytes
                                           : spec.pattern_memory_bytes;
        const std::size_t pattern_bytes = static_cast<std::size_t>(
            std::min<std::uint64_t>(spec.memory_bytes, requested_pattern_bytes));
        for (std::size_t offset = 0; offset + 4 <= pattern_bytes; offset += 4) {
            memory.write_u32(offset, static_cast<std::uint32_t>(offset ^ 0x5A5A5A5Au));
        }
        // Re-assert the program words after the pattern fill.
        for (std::size_t i = 0; i < program.size(); ++i) {
            memory.write_u32(i * 4, program[i]);
        }
    }

    cpu.set_program_counter(0);

    snapshot::SnapshotRestoreContext ctx{};
    ctx.primary_cpu = &cpu;
    ctx.memory = &memory;
    ctx.virtual_time = &vt;
    ctx.gic = &gic;

    snapshot::SnapshotCaptureOptions opts{};
    opts.description = spec.description;
    opts.creator = "WinIOS Stage 21 workload generator";
    const auto cap_res = snapshot::SnapshotManager::capture_to_file(
        ctx, spec.wsnap_path, opts);
    if (!cap_res.success) {
        result.message = "snapshot capture failed: " + cap_res.error_message;
        return result;
    }

    snapshot::SnapshotInspection inspection{};
    const auto inspect_res = snapshot::SnapshotManager::inspect_file(
        spec.wsnap_path, inspection);
    if (!inspect_res.success) {
        result.message = "snapshot inspect failed: " + inspect_res.error_message;
        return result;
    }
    result.base_snapshot_hash = inspection.header.header_crc32c;

    sndl::WSndlHeader header{};
    header.core_count = 1;
    header.total_instructions = spec.instructions;
    header.base_snapshot_hash = inspection.header.header_crc32c;

    sndl::SndlWriter writer;
    const auto open_res = writer.open(spec.sndl_path, header);
    if (open_res.is_failure()) {
        result.message = "SNDL open failed: " + open_res.message;
        return result;
    }

    if (spec.include_counter_events) {
        const std::uint64_t record_count = spec.instructions / spec.counter_event_stride;
        std::uint64_t sequence = 1;
        for (std::uint64_t retirement = 0;
             retirement < spec.instructions;
             retirement += spec.counter_event_stride) {
            auto record = sndl::make_counter_read_record(
                spec.core_id,
                0,
                retirement,
                0,
                spec.counter_reg_key,
                retirement,
                spec.counter_frequency);
            record.sequence_num = sequence++;
            const auto write_res = writer.write_record(record);
            if (write_res.is_failure()) {
                result.message = "SNDL record write failed: " + write_res.message;
                return result;
            }
        }
        result.total_records = record_count;
    }

    const auto finalize_res = writer.finalize(
        sndl::SndlFinalizationStatus::Completed, spec.instructions);
    if (finalize_res.is_failure()) {
        result.message = "SNDL finalize failed: " + finalize_res.message;
        return result;
    }

    result.total_records = writer.total_records_written();
    result.wsnap_bytes = file_size_or_zero(spec.wsnap_path);
    result.sndl_bytes = file_size_or_zero(spec.sndl_path);
    result.success = true;
    result.message = "workload generated";
    return result;
}

} // namespace winios::replay
