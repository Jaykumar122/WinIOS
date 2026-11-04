#pragma once

#include "sndl/sndl_format.h"
#include "sndl/sndl_types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace winios::sndl {

// -----------------------------------------------------------------------------
// SndlWriter: Block-Oriented Bounded Disk Writer for .sndl Files
// -----------------------------------------------------------------------------
class SndlWriter {
public:
    explicit SndlWriter(std::uint32_t max_records_per_block = kDefaultMaxRecordsPerBlock);
    ~SndlWriter();

    SndlWriter(const SndlWriter&) = delete;
    SndlWriter& operator=(const SndlWriter&) = delete;
    SndlWriter(SndlWriter&&) noexcept;
    SndlWriter& operator=(SndlWriter&&) noexcept;

    // Opens and initializes an .sndl file with initial header
    [[nodiscard]] SndlResult open(const std::filesystem::path& file_path,
                                  const WSndlHeader& initial_header);

    // Appends a record to the active block buffer; flushes to disk when block is full
    [[nodiscard]] SndlResult write_record(const SndlRecord& record);

    // Flushes any partially-filled block buffer to disk
    [[nodiscard]] SndlResult flush();

    // Finalizes the file cleanly with WSndlFooter and updates header statistics
    [[nodiscard]] SndlResult finalize(SndlFinalizationStatus status,
                                      std::uint64_t total_instructions = 0);

    // Closes the file without finalizing (used on abort / failure)
    void close() noexcept;

    // Status queries
    [[nodiscard]] bool is_open() const noexcept { return file_.is_open(); }
    [[nodiscard]] bool has_failed() const noexcept { return failed_; }
    [[nodiscard]] std::uint64_t total_records_written() const noexcept { return total_records_; }
    [[nodiscard]] std::uint32_t total_blocks_written() const noexcept { return block_count_; }
    [[nodiscard]] std::uint64_t last_sequence_written() const noexcept { return last_sequence_; }

private:
    [[nodiscard]] SndlResult write_block_locked();

    std::uint32_t max_records_per_block_{kDefaultMaxRecordsPerBlock};
    std::filesystem::path file_path_;
    std::ofstream file_;
    WSndlHeader header_{};
    std::vector<SndlRecord> block_records_;
    std::vector<std::uint8_t> block_payload_buffer_;
    std::uint64_t total_records_{0};
    std::uint32_t block_count_{0};
    std::uint64_t first_sequence_{0};
    std::uint64_t last_sequence_{0};
    bool failed_{false};
    bool finalized_{false};
};

} // namespace winios::sndl
