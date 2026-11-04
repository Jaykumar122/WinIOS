#pragma once

#include "sndl/sndl_recorder.h"
#include <cstdint>

namespace winios::sndl {

// Global active recorder binding (thread-safe pointer exchange)
void set_active_recorder(SndlRecorder* recorder) noexcept;
[[nodiscard]] SndlRecorder* get_active_recorder() noexcept;

// High-performance non-invasive hook points called from CPU, GICv3, and devices
inline void hook_mmio_read(std::uint16_t core_id, std::uint64_t pc,
                           std::uint64_t ret_count, std::uint64_t paddr,
                           std::uint64_t value, unsigned width,
                           std::uint32_t source_id = 0) {
    if (auto* rec = get_active_recorder()) {
        rec->record_mmio_read(core_id, pc, ret_count, paddr, value, width, source_id);
    }
}

inline void hook_interrupt_inject(std::uint16_t core_id, std::uint32_t intid,
                                  std::uint8_t priority, std::uint32_t flags = 0,
                                  std::uint32_t source_id = 0) {
    if (auto* rec = get_active_recorder()) {
        rec->record_interrupt_inject(core_id, intid, priority, flags, source_id);
    }
}

inline void hook_interrupt_delivery(std::uint16_t core_id, std::uint64_t pc,
                                    std::uint64_t ret_count, std::uint32_t intid,
                                    std::uint8_t priority, bool is_fiq,
                                    bool is_virtual, unsigned target_el) {
    if (auto* rec = get_active_recorder()) {
        rec->record_interrupt_delivery(core_id, pc, ret_count, intid, priority, is_fiq, is_virtual, target_el);
    }
}

inline void hook_timer_expiration(std::uint16_t core_id, std::uint64_t pc,
                                  std::uint64_t ret_count, std::uint8_t timer_type,
                                  std::uint64_t cval, std::uint64_t counter) {
    if (auto* rec = get_active_recorder()) {
        rec->record_timer_expiration(core_id, pc, ret_count, timer_type, cval, counter);
    }
}

inline void hook_counter_read(std::uint16_t core_id, std::uint64_t pc,
                              std::uint64_t ret_count, std::uint32_t reg_key,
                              std::uint64_t counter_val, std::uint64_t frequency) {
    if (auto* rec = get_active_recorder()) {
        rec->record_counter_read(core_id, pc, ret_count, reg_key, counter_val, frequency);
    }
}

inline void hook_entropy(std::uint16_t core_id, std::uint64_t pc,
                         std::uint64_t ret_count, std::uint64_t value,
                         unsigned bit_width = 64, std::uint32_t source_id = 0) {
    if (auto* rec = get_active_recorder()) {
        rec->record_entropy(core_id, pc, ret_count, value, bit_width, source_id);
    }
}

inline void hook_host_input(std::uint16_t core_id, std::uint32_t channel_id,
                            std::uint64_t data_payload, std::size_t length) {
    if (auto* rec = get_active_recorder()) {
        rec->record_host_input(core_id, channel_id, data_payload, length);
    }
}

} // namespace winios::sndl
