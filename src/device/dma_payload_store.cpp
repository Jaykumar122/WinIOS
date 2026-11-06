#include "device/dma_payload_store.h"
#include "trace/trace_serializer.h"

#include <cstring>
#include <iostream>

namespace winios::device {

bool DmaPayloadRecord::verify_crc() const noexcept {
    if (data.empty()) return true;
    const std::uint32_t calc = trace::calculate_crc32c(data.data(), data.size());
    return calc == payload_crc32c;
}

DmaPayloadStore::~DmaPayloadStore() {
    clear();
}

DmaPayloadStore::DmaPayloadStore(DmaPayloadStore&& other) noexcept
    : file_path_(std::move(other.file_path_)),
      write_file_(std::move(other.write_file_)),
      read_file_(std::move(other.read_file_)),
      is_writing_(other.is_writing_),
      is_reading_(other.is_reading_),
      finalized_(other.finalized_),
      session_id_(other.session_id_),
      base_snapshot_hash_(other.base_snapshot_hash_),
      lifecycle_state_(other.lifecycle_state_),
      index_offset_(other.index_offset_),
      total_records_(other.total_records_),
      total_bytes_(other.total_bytes_),
      records_(std::move(other.records_)),
      index_entries_(std::move(other.index_entries_)) {
    other.is_writing_ = false;
    other.is_reading_ = false;
    other.finalized_ = false;
    other.session_id_ = 0;
    other.base_snapshot_hash_ = 0;
    other.lifecycle_state_ = RecordingLifecycleState::Created;
    other.index_offset_ = 0;
    other.total_records_ = 0;
    other.total_bytes_ = 0;
}

DmaPayloadStore& DmaPayloadStore::operator=(DmaPayloadStore&& other) noexcept {
    if (this != &other) {
        clear();
        file_path_ = std::move(other.file_path_);
        write_file_ = std::move(other.write_file_);
        read_file_ = std::move(other.read_file_);
        is_writing_ = other.is_writing_;
        is_reading_ = other.is_reading_;
        finalized_ = other.finalized_;
        session_id_ = other.session_id_;
        base_snapshot_hash_ = other.base_snapshot_hash_;
        lifecycle_state_ = other.lifecycle_state_;
        index_offset_ = other.index_offset_;
        total_records_ = other.total_records_;
        total_bytes_ = other.total_bytes_;
        records_ = std::move(other.records_);
        index_entries_ = std::move(other.index_entries_);

        other.is_writing_ = false;
        other.is_reading_ = false;
        other.finalized_ = false;
        other.session_id_ = 0;
        other.base_snapshot_hash_ = 0;
        other.lifecycle_state_ = RecordingLifecycleState::Created;
        other.index_offset_ = 0;
        other.total_records_ = 0;
        other.total_bytes_ = 0;
    }
    return *this;
}

void DmaPayloadStore::clear() noexcept {
    if (write_file_.is_open()) {
        write_file_.close();
    }
    if (read_file_.is_open()) {
        read_file_.close();
    }
    is_writing_ = false;
    is_reading_ = false;
    finalized_ = false;
    session_id_ = 0;
    base_snapshot_hash_ = 0;
    lifecycle_state_ = RecordingLifecycleState::Created;
    index_offset_ = 0;
    total_records_ = 0;
    total_bytes_ = 0;
    file_path_.clear();
    records_.clear();
    index_entries_.clear();
}

void DmaPayloadStore::insert_in_memory_payload(DmaPayloadRecord record) {
    total_records_++;
    total_bytes_ += record.data.size();
    records_[record.global_sequence] = std::move(record);
}

DmaPayloadStoreResult DmaPayloadStore::open_write(
    const std::filesystem::path& file_path,
    std::uint64_t session_id,
    std::uint64_t base_snapshot_hash) {

    clear();
    file_path_ = file_path;
    session_id_ = session_id;
    base_snapshot_hash_ = base_snapshot_hash;
    lifecycle_state_ = RecordingLifecycleState::Recording;

    write_file_.open(file_path_, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!write_file_.is_open()) {
        return DmaPayloadStoreResult::fail(DmaPayloadStoreError::FileOpenFailed,
                                           "Failed to open companion DMA file for writing: " + file_path_.string());
    }

    // Write placeholder 64-byte Version 2 header (marked Recording)
    std::uint8_t hdr[kDmaDataHeaderSize]{};
    trace::encode_u64_le(hdr + 0, kDmaDataFileMagic);
    trace::encode_u16_le(hdr + 8, kDmaDataVersion2);
    hdr[10] = static_cast<std::uint8_t>(lifecycle_state_);
    hdr[11] = 0; // reserved
    trace::encode_u32_le(hdr + 12, 0); // flags
    trace::encode_u64_le(hdr + 16, 0); // total_records placeholder
    trace::encode_u64_le(hdr + 24, 0); // total_bytes placeholder
    trace::encode_u64_le(hdr + 32, session_id_);
    trace::encode_u64_le(hdr + 40, base_snapshot_hash_);
    trace::encode_u64_le(hdr + 48, 0); // index_offset placeholder
    trace::encode_u32_le(hdr + 56, 0); // index_record_count placeholder

    const std::uint32_t crc = trace::calculate_crc32c(hdr, 60);
    trace::encode_u32_le(hdr + 60, crc);

    write_file_.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
    if (!write_file_) {
        clear();
        return DmaPayloadStoreResult::fail(DmaPayloadStoreError::FileWriteFailed,
                                           "Failed to write header to companion DMA file");
    }

    is_writing_ = true;
    finalized_ = false;
    return DmaPayloadStoreResult::ok();
}

DmaPayloadStoreResult DmaPayloadStore::record_payload(
    std::uint64_t global_sequence,
    std::uint64_t descriptor_id,
    std::uint64_t dst_paddr,
    const std::uint8_t* data,
    std::size_t size) {

    const std::uint32_t crc = (data && size > 0) ? trace::calculate_crc32c(data, size) : 0;

    // Cache in memory
    DmaPayloadRecord rec;
    rec.global_sequence = global_sequence;
    rec.descriptor_id = descriptor_id;
    rec.dst_paddr = dst_paddr;
    if (data && size > 0) {
        rec.data.assign(data, data + size);
    }
    rec.payload_crc32c = crc;

    records_[global_sequence] = rec;
    total_records_++;
    total_bytes_ += size;

    if (is_writing_ && write_file_.is_open()) {
        const std::uint64_t entry_file_offset = static_cast<std::uint64_t>(write_file_.tellp());

        std::uint8_t entry_hdr[kDmaDataEntryHeaderSize]{};
        trace::encode_u64_le(entry_hdr + 0, kDmaDataEntryMagic);
        trace::encode_u64_le(entry_hdr + 8, global_sequence);
        trace::encode_u64_le(entry_hdr + 16, descriptor_id);
        trace::encode_u64_le(entry_hdr + 24, dst_paddr);
        trace::encode_u64_le(entry_hdr + 32, static_cast<std::uint64_t>(size));
        trace::encode_u32_le(entry_hdr + 40, crc);

        const std::uint32_t hdr_crc = trace::calculate_crc32c(entry_hdr, 44);
        trace::encode_u32_le(entry_hdr + 44, hdr_crc);

        write_file_.write(reinterpret_cast<const char*>(entry_hdr), sizeof(entry_hdr));
        if (data && size > 0) {
            write_file_.write(reinterpret_cast<const char*>(data), size);
        }

        if (!write_file_) {
            return DmaPayloadStoreResult::fail(DmaPayloadStoreError::FileWriteFailed,
                                               "Failed to write payload to companion DMA file");
        }

        // Record for index table
        DmaPayloadIndexEntry idx{};
        idx.global_sequence = global_sequence;
        idx.descriptor_id = descriptor_id;
        idx.dst_paddr = dst_paddr;
        idx.file_offset = entry_file_offset;
        idx.length = size;
        idx.payload_crc32c = crc;
        index_entries_.push_back(idx);
    }

    return DmaPayloadStoreResult::ok();
}

DmaPayloadStoreResult DmaPayloadStore::finalize(RecordingLifecycleState final_state) {
    if (!is_writing_) {
        return DmaPayloadStoreResult::ok();
    }
    if (finalized_) {
        return DmaPayloadStoreResult::fail(DmaPayloadStoreError::AlreadyFinalized);
    }

    lifecycle_state_ = final_state;

    if (write_file_.is_open()) {
        std::uint64_t idx_offset = 0;
        const std::uint32_t idx_count = static_cast<std::uint32_t>(index_entries_.size());

        if ((final_state == RecordingLifecycleState::Complete || final_state == RecordingLifecycleState::RecoveredIncomplete) && !index_entries_.empty()) {
            idx_offset = static_cast<std::uint64_t>(write_file_.tellp());

            // Write index table entries
            for (const auto& entry : index_entries_) {
                std::uint8_t buf[kDmaDataIndexEntrySize]{};
                trace::encode_u64_le(buf + 0, entry.global_sequence);
                trace::encode_u64_le(buf + 8, entry.descriptor_id);
                trace::encode_u64_le(buf + 16, entry.dst_paddr);
                trace::encode_u64_le(buf + 24, entry.file_offset);
                trace::encode_u64_le(buf + 32, entry.length);
                trace::encode_u32_le(buf + 40, entry.payload_crc32c);
                trace::encode_u32_le(buf + 44, 0); // reserved
                write_file_.write(reinterpret_cast<const char*>(buf), sizeof(buf));
            }
        }

        index_offset_ = idx_offset;

        // Rewrite complete header at offset 0
        std::uint8_t hdr[kDmaDataHeaderSize]{};
        trace::encode_u64_le(hdr + 0, kDmaDataFileMagic);
        trace::encode_u16_le(hdr + 8, kDmaDataVersion2);
        hdr[10] = static_cast<std::uint8_t>(lifecycle_state_);
        hdr[11] = 0;
        trace::encode_u32_le(hdr + 12, 0);
        trace::encode_u64_le(hdr + 16, total_records_);
        trace::encode_u64_le(hdr + 24, total_bytes_);
        trace::encode_u64_le(hdr + 32, session_id_);
        trace::encode_u64_le(hdr + 40, base_snapshot_hash_);
        trace::encode_u64_le(hdr + 48, index_offset_);
        trace::encode_u32_le(hdr + 56, idx_count);

        const std::uint32_t crc = trace::calculate_crc32c(hdr, 60);
        trace::encode_u32_le(hdr + 60, crc);

        write_file_.seekp(0, std::ios::beg);
        write_file_.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
        write_file_.flush();
        write_file_.close();
    }

    finalized_ = true;
    is_writing_ = false;
    return DmaPayloadStoreResult::ok();
}

DmaPayloadStoreResult DmaPayloadStore::open_read(
    const std::filesystem::path& file_path,
    std::uint64_t expected_session_id,
    std::uint64_t expected_snapshot_hash) {

    clear();
    file_path_ = file_path;

    if (!std::filesystem::exists(file_path_)) {
        return DmaPayloadStoreResult::fail(DmaPayloadStoreError::FileNotFound,
                                           "Companion DMA file not found: " + file_path_.string());
    }

    read_file_.open(file_path_, std::ios::binary | std::ios::in);
    if (!read_file_.is_open()) {
        return DmaPayloadStoreResult::fail(DmaPayloadStoreError::FileOpenFailed,
                                           "Failed to open companion DMA file for reading: " + file_path_.string());
    }

    // Read and validate header
    std::uint8_t hdr[kDmaDataHeaderSize]{};
    read_file_.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    if (read_file_.gcount() < static_cast<std::streamsize>(sizeof(hdr))) {
        clear();
        return DmaPayloadStoreResult::fail(DmaPayloadStoreError::TruncatedPayload, "Header truncated");
    }

    const std::uint64_t magic = trace::decode_u64_le(hdr + 0);
    if (magic != kDmaDataFileMagic) {
        clear();
        return DmaPayloadStoreResult::fail(DmaPayloadStoreError::InvalidMagic, "Invalid magic in companion DMA header");
    }

    const std::uint16_t version = trace::decode_u16_le(hdr + 8);
    if (version != kDmaDataVersion1 && version != kDmaDataVersion2) {
        clear();
        return DmaPayloadStoreResult::fail(DmaPayloadStoreError::UnsupportedVersion, "Unsupported version in companion DMA header");
    }

    const std::uint32_t expected_crc = trace::decode_u32_le(hdr + 60);
    const std::uint32_t calc_crc = trace::calculate_crc32c(hdr, 60);
    if (expected_crc != calc_crc) {
        clear();
        return DmaPayloadStoreResult::fail(DmaPayloadStoreError::HeaderCrcMismatch, "Header CRC32C mismatch in companion DMA file");
    }

    if (version >= kDmaDataVersion2) {
        lifecycle_state_ = static_cast<RecordingLifecycleState>(hdr[10]);
        session_id_ = trace::decode_u64_le(hdr + 32);
        base_snapshot_hash_ = trace::decode_u64_le(hdr + 40);
        index_offset_ = trace::decode_u64_le(hdr + 48);

        // Cross-file identity validation
        if (expected_session_id != 0 && session_id_ != expected_session_id) {
            clear();
            return DmaPayloadStoreResult::fail(DmaPayloadStoreError::SessionMismatch,
                "Companion DMA session ID does not match expected session ID");
        }
        if (expected_snapshot_hash != 0 && base_snapshot_hash_ != expected_snapshot_hash) {
            clear();
            return DmaPayloadStoreResult::fail(DmaPayloadStoreError::SnapshotMismatch,
                "Companion DMA base snapshot hash does not match expected snapshot hash");
        }
    } else {
        lifecycle_state_ = RecordingLifecycleState::Complete; // v1 legacy assumed finalized
    }

    // Read all entries sequentially
    read_file_.seekg(kDmaDataHeaderSize, std::ios::beg);
    while (true) {
        // If we reached index_offset_, we've read all data entries
        if (index_offset_ != 0 && static_cast<std::uint64_t>(read_file_.tellg()) >= index_offset_) {
            break;
        }

        std::uint8_t entry_hdr[kDmaDataEntryHeaderSize]{};
        read_file_.read(reinterpret_cast<char*>(entry_hdr), sizeof(entry_hdr));
        const auto bytes_read = read_file_.gcount();
        if (bytes_read == 0) {
            break; // Clean EOF
        }
        if (bytes_read < static_cast<std::streamsize>(sizeof(entry_hdr))) {
            clear();
            return DmaPayloadStoreResult::fail(DmaPayloadStoreError::TruncatedPayload, "Entry header truncated");
        }

        const std::uint64_t emagic = trace::decode_u64_le(entry_hdr + 0);
        if (emagic != kDmaDataEntryMagic) {
            clear();
            return DmaPayloadStoreResult::fail(DmaPayloadStoreError::InvalidMagic, "Invalid entry magic");
        }

        const std::uint32_t expected_hdr_crc = trace::decode_u32_le(entry_hdr + 44);
        const std::uint32_t calc_hdr_crc = trace::calculate_crc32c(entry_hdr, 44);
        if (expected_hdr_crc != calc_hdr_crc) {
            clear();
            return DmaPayloadStoreResult::fail(DmaPayloadStoreError::EntryCrcMismatch, "Entry header CRC32C mismatch");
        }

        const std::uint64_t gseq = trace::decode_u64_le(entry_hdr + 8);
        const std::uint64_t desc_id = trace::decode_u64_le(entry_hdr + 16);
        const std::uint64_t dst_paddr = trace::decode_u64_le(entry_hdr + 24);
        const std::uint64_t length = trace::decode_u64_le(entry_hdr + 32);
        const std::uint32_t payload_crc = trace::decode_u32_le(entry_hdr + 40);

        DmaPayloadRecord rec;
        rec.global_sequence = gseq;
        rec.descriptor_id = desc_id;
        rec.dst_paddr = dst_paddr;
        rec.payload_crc32c = payload_crc;

        constexpr std::uint64_t kMaxSupportedPayloadSize = 64 * 1024 * 1024; // 64 MB
        if (length > kMaxSupportedPayloadSize) {
            clear();
            return DmaPayloadStoreResult::fail(DmaPayloadStoreError::InvalidLength, "Payload length exceeds maximum allowed limit");
        }

        if (length > 0) {
            rec.data.resize(static_cast<std::size_t>(length));
            read_file_.read(reinterpret_cast<char*>(rec.data.data()), static_cast<std::streamsize>(length));
            if (read_file_.gcount() < static_cast<std::streamsize>(length)) {
                clear();
                return DmaPayloadStoreResult::fail(DmaPayloadStoreError::TruncatedPayload, "Payload data truncated");
            }

            const std::uint32_t actual_payload_crc = trace::calculate_crc32c(rec.data.data(), rec.data.size());
            if (actual_payload_crc != payload_crc) {
                clear();
                return DmaPayloadStoreResult::fail(DmaPayloadStoreError::PayloadCrcMismatch, "Payload data CRC32C mismatch");
            }
        }

        records_[gseq] = std::move(rec);
        total_records_++;
        total_bytes_ += length;
    }

    is_reading_ = true;
    return DmaPayloadStoreResult::ok();
}

const DmaPayloadRecord* DmaPayloadStore::find_payload(std::uint64_t global_sequence) const noexcept {
    auto it = records_.find(global_sequence);
    if (it != records_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool DmaPayloadStore::has_payload(std::uint64_t global_sequence) const noexcept {
    return records_.find(global_sequence) != records_.end();
}

} // namespace winios::device
