#include "cpu/smp_shared_memory.h"

#include <cstring>
#include <stdexcept>

namespace winios {

SmpSharedMemory::SmpSharedMemory(std::size_t size_bytes, std::size_t max_cores)
    : memory_(std::make_shared<Memory>(size_bytes)),
      telemetry_(max_cores) {}

SmpSharedMemory::SmpSharedMemory(std::shared_ptr<Memory> memory, std::size_t max_cores)
    : memory_(std::move(memory)),
      telemetry_(max_cores) {
    if (!memory_) {
        throw std::invalid_argument("memory pointer cannot be null in SmpSharedMemory");
    }
}

void SmpSharedMemory::ensure_core_telemetry(std::size_t core_id) const {
    if (core_id >= telemetry_.size()) {
        telemetry_.resize(core_id + 1);
    }
}

std::uint8_t SmpSharedMemory::read_u8(std::size_t core_id, std::uint64_t address) {
    ensure_core_telemetry(core_id);
    telemetry_[core_id].reads++;
    telemetry_[core_id].bytes_read += 1;
    return memory_->read_u8(static_cast<std::size_t>(address));
}

void SmpSharedMemory::write_u8(std::size_t core_id, std::uint64_t address, std::uint8_t value) {
    ensure_core_telemetry(core_id);
    telemetry_[core_id].writes++;
    telemetry_[core_id].bytes_written += 1;
    memory_->write_u8(static_cast<std::size_t>(address), value);
    if (exclusive_monitor_) {
        exclusive_monitor_->invalidate_reservations(core_id, address, 1);
    }
}

std::uint16_t SmpSharedMemory::read_u16(std::size_t core_id, std::uint64_t address) {
    ensure_core_telemetry(core_id);
    telemetry_[core_id].reads++;
    telemetry_[core_id].bytes_read += 2;
    return memory_->read_u16(static_cast<std::size_t>(address));
}

void SmpSharedMemory::write_u16(std::size_t core_id, std::uint64_t address, std::uint16_t value) {
    ensure_core_telemetry(core_id);
    telemetry_[core_id].writes++;
    telemetry_[core_id].bytes_written += 2;
    memory_->write_u16(static_cast<std::size_t>(address), value);
    if (exclusive_monitor_) {
        exclusive_monitor_->invalidate_reservations(core_id, address, 2);
    }
}

std::uint32_t SmpSharedMemory::read_u32(std::size_t core_id, std::uint64_t address) {
    ensure_core_telemetry(core_id);
    telemetry_[core_id].reads++;
    telemetry_[core_id].bytes_read += 4;
    return memory_->read_u32(static_cast<std::size_t>(address));
}

void SmpSharedMemory::write_u32(std::size_t core_id, std::uint64_t address, std::uint32_t value) {
    ensure_core_telemetry(core_id);
    telemetry_[core_id].writes++;
    telemetry_[core_id].bytes_written += 4;
    memory_->write_u32(static_cast<std::size_t>(address), value);
    if (exclusive_monitor_) {
        exclusive_monitor_->invalidate_reservations(core_id, address, 4);
    }
}

std::uint64_t SmpSharedMemory::read_u64(std::size_t core_id, std::uint64_t address) {
    ensure_core_telemetry(core_id);
    telemetry_[core_id].reads++;
    telemetry_[core_id].bytes_read += 8;
    return memory_->read_u64(static_cast<std::size_t>(address));
}

void SmpSharedMemory::write_u64(std::size_t core_id, std::uint64_t address, std::uint64_t value) {
    ensure_core_telemetry(core_id);
    telemetry_[core_id].writes++;
    telemetry_[core_id].bytes_written += 8;
    memory_->write_u64(static_cast<std::size_t>(address), value);
    if (exclusive_monitor_) {
        exclusive_monitor_->invalidate_reservations(core_id, address, 8);
    }
}

void SmpSharedMemory::read_bytes(std::size_t core_id, std::uint64_t address, void* dst, std::size_t size) {
    if (size == 0) return;
    ensure_core_telemetry(core_id);
    telemetry_[core_id].reads++;
    telemetry_[core_id].bytes_read += size;
    auto* d = static_cast<std::uint8_t*>(dst);
    for (std::size_t i = 0; i < size; ++i) {
        d[i] = memory_->read_u8(static_cast<std::size_t>(address + i));
    }
}

void SmpSharedMemory::write_bytes(std::size_t core_id, std::uint64_t address, const void* src, std::size_t size) {
    if (size == 0) return;
    ensure_core_telemetry(core_id);
    telemetry_[core_id].writes++;
    telemetry_[core_id].bytes_written += size;
    const auto* s = static_cast<const std::uint8_t*>(src);
    for (std::size_t i = 0; i < size; ++i) {
        memory_->write_u8(static_cast<std::size_t>(address + i), s[i]);
    }
    if (exclusive_monitor_) {
        exclusive_monitor_->invalidate_reservations(core_id, address, size);
    }
}

bool SmpSharedMemory::write_exclusive_u8(std::size_t core_id, std::uint64_t address, std::uint8_t value) {
    if (!exclusive_monitor_) {
        write_u8(core_id, address, value);
        return true;
    }
    return exclusive_monitor_->store_exclusive(core_id, address, 1, [this, core_id, address, value]() {
        ensure_core_telemetry(core_id);
        telemetry_[core_id].writes++;
        telemetry_[core_id].bytes_written += 1;
        memory_->write_u8(static_cast<std::size_t>(address), value);
    });
}

bool SmpSharedMemory::write_exclusive_u16(std::size_t core_id, std::uint64_t address, std::uint16_t value) {
    if (!exclusive_monitor_) {
        write_u16(core_id, address, value);
        return true;
    }
    return exclusive_monitor_->store_exclusive(core_id, address, 2, [this, core_id, address, value]() {
        ensure_core_telemetry(core_id);
        telemetry_[core_id].writes++;
        telemetry_[core_id].bytes_written += 2;
        memory_->write_u16(static_cast<std::size_t>(address), value);
    });
}

bool SmpSharedMemory::write_exclusive_u32(std::size_t core_id, std::uint64_t address, std::uint32_t value) {
    if (!exclusive_monitor_) {
        write_u32(core_id, address, value);
        return true;
    }
    return exclusive_monitor_->store_exclusive(core_id, address, 4, [this, core_id, address, value]() {
        ensure_core_telemetry(core_id);
        telemetry_[core_id].writes++;
        telemetry_[core_id].bytes_written += 4;
        memory_->write_u32(static_cast<std::size_t>(address), value);
    });
}

bool SmpSharedMemory::write_exclusive_u64(std::size_t core_id, std::uint64_t address, std::uint64_t value) {
    if (!exclusive_monitor_) {
        write_u64(core_id, address, value);
        return true;
    }
    return exclusive_monitor_->store_exclusive(core_id, address, 8, [this, core_id, address, value]() {
        ensure_core_telemetry(core_id);
        telemetry_[core_id].writes++;
        telemetry_[core_id].bytes_written += 8;
        memory_->write_u64(static_cast<std::size_t>(address), value);
    });
}

CoreMemoryTelemetry SmpSharedMemory::get_telemetry(std::size_t core_id) const {
    if (core_id < telemetry_.size()) {
        return telemetry_[core_id];
    }
    return CoreMemoryTelemetry{};
}

void SmpSharedMemory::reset_telemetry() noexcept {
    for (auto& t : telemetry_) {
        t = CoreMemoryTelemetry{};
    }
}

} // namespace winios
