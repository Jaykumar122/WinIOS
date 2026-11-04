#pragma once

#include "sndl/sndl_types.h"
#include <cstddef>
#include <cstdint>

namespace winios::sndl {

// -----------------------------------------------------------------------------
// Record Serialization & Deserialization (Canonical Little-Endian 64 Bytes)
// -----------------------------------------------------------------------------
void encode_record(const SndlRecord& rec, std::uint8_t* dst) noexcept;
[[nodiscard]] SndlResult decode_record(const std::uint8_t* src, std::size_t size, SndlRecord& out_rec) noexcept;

// Source-specific record builder helpers
SndlRecord make_mmio_read_record(std::uint16_t core_id, std::uint64_t pc,
                                 std::uint64_t ret_count, std::uint16_t sub_ord,
                                 std::uint64_t paddr, std::uint64_t value,
                                 unsigned width, std::uint32_t source_id = 0) noexcept;

SndlRecord make_interrupt_inject_record(std::uint16_t core_id, std::uint32_t intid,
                                       std::uint8_t priority, std::uint32_t flags,
                                       std::uint32_t source_id = 0) noexcept;

SndlRecord make_interrupt_delivery_record(std::uint16_t core_id, std::uint64_t pc,
                                         std::uint64_t ret_count, std::uint32_t intid,
                                         std::uint8_t priority, bool is_fiq,
                                         bool is_virtual, unsigned target_el) noexcept;

SndlRecord make_timer_expiration_record(std::uint16_t core_id, std::uint64_t pc,
                                       std::uint64_t ret_count, std::uint8_t timer_type,
                                       std::uint64_t cval, std::uint64_t counter) noexcept;

SndlRecord make_counter_read_record(std::uint16_t core_id, std::uint64_t pc,
                                   std::uint64_t ret_count, std::uint16_t sub_ord,
                                   std::uint32_t reg_key, std::uint64_t counter_val,
                                   std::uint64_t frequency) noexcept;

SndlRecord make_entropy_record(std::uint16_t core_id, std::uint64_t pc,
                              std::uint64_t ret_count, std::uint16_t sub_ord,
                              std::uint64_t value, unsigned bit_width,
                              std::uint32_t source_id = 0) noexcept;

SndlRecord make_host_input_record(std::uint16_t core_id, std::uint32_t channel_id,
                                 std::uint64_t data_payload, std::size_t length) noexcept;

} // namespace winios::sndl
