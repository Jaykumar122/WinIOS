#include "sndl/sndl_event_encoder.h"
#include "trace/trace_serializer.h"
#include <cstring>

namespace winios::sndl {

void encode_record(const SndlRecord& rec, std::uint8_t* dst) noexcept {
    trace::encode_u64_le(dst + 0,  rec.sequence_num);
    trace::encode_u64_le(dst + 8,  rec.instruction_retirement_count);
    trace::encode_u64_le(dst + 16, rec.pc);
    trace::encode_u64_le(dst + 24, rec.payload1);
    trace::encode_u64_le(dst + 32, rec.payload2);
    trace::encode_u64_le(dst + 40, rec.payload3);
    trace::encode_u32_le(dst + 48, rec.source_id);
    trace::encode_u32_le(dst + 52, rec.flags);
    trace::encode_u16_le(dst + 56, rec.core_id);
    trace::encode_u16_le(dst + 58, rec.sub_instruction_ordinal);
    dst[60] = static_cast<std::uint8_t>(rec.event_type);
    dst[61] = static_cast<std::uint8_t>(rec.boundary_type);
    trace::encode_u16_le(dst + 62, rec.payload_size);
}

SndlResult decode_record(const std::uint8_t* src, std::size_t size, SndlRecord& out_rec) noexcept {
    if (size < kWSndlRecordDiskSize) {
        return SndlResult::fail(SndlErrorCode::FileTooShort, "Buffer too short for SNDL record");
    }

    out_rec.sequence_num                 = trace::decode_u64_le(src + 0);
    out_rec.instruction_retirement_count = trace::decode_u64_le(src + 8);
    out_rec.pc                           = trace::decode_u64_le(src + 16);
    out_rec.payload1                     = trace::decode_u64_le(src + 24);
    out_rec.payload2                     = trace::decode_u64_le(src + 32);
    out_rec.payload3                     = trace::decode_u64_le(src + 40);
    out_rec.source_id                    = trace::decode_u32_le(src + 48);
    out_rec.flags                        = trace::decode_u32_le(src + 52);
    out_rec.core_id                      = trace::decode_u16_le(src + 56);
    out_rec.sub_instruction_ordinal      = trace::decode_u16_le(src + 58);
    out_rec.event_type                   = static_cast<SndlEventType>(src[60]);
    out_rec.boundary_type                = static_cast<SndlBoundaryType>(src[61]);
    out_rec.payload_size                 = trace::decode_u16_le(src + 62);

    return SndlResult::ok();
}

SndlRecord make_mmio_read_record(std::uint16_t core_id, std::uint64_t pc,
                                 std::uint64_t ret_count, std::uint16_t sub_ord,
                                 std::uint64_t paddr, std::uint64_t value,
                                 unsigned width, std::uint32_t source_id) noexcept {
    SndlRecord rec{};
    rec.core_id = core_id;
    rec.pc = pc;
    rec.instruction_retirement_count = ret_count;
    rec.sub_instruction_ordinal = sub_ord;
    rec.event_type = SndlEventType::MmioReadResult;
    rec.boundary_type = SndlBoundaryType::DuringInstruction;
    rec.payload1 = paddr;
    rec.payload2 = value;
    rec.payload3 = static_cast<std::uint64_t>(width);
    rec.source_id = source_id;
    rec.payload_size = static_cast<std::uint16_t>(width);
    return rec;
}

SndlRecord make_interrupt_inject_record(std::uint16_t core_id, std::uint32_t intid,
                                       std::uint8_t priority, std::uint32_t flags,
                                       std::uint32_t source_id) noexcept {
    SndlRecord rec{};
    rec.core_id = core_id;
    rec.event_type = SndlEventType::ExternalInterruptInject;
    rec.boundary_type = SndlBoundaryType::AsynchronousInput;
    rec.payload1 = intid;
    rec.payload2 = priority;
    rec.payload3 = flags;
    rec.source_id = source_id;
    rec.payload_size = 4;
    return rec;
}

SndlRecord make_interrupt_delivery_record(std::uint16_t core_id, std::uint64_t pc,
                                         std::uint64_t ret_count, std::uint32_t intid,
                                         std::uint8_t priority, bool is_fiq,
                                         bool is_virtual, unsigned target_el) noexcept {
    SndlRecord rec{};
    rec.core_id = core_id;
    rec.pc = pc;
    rec.instruction_retirement_count = ret_count;
    rec.event_type = SndlEventType::InterruptDelivery;
    rec.boundary_type = SndlBoundaryType::ExceptionBoundary;
    rec.payload1 = intid;
    rec.payload2 = (static_cast<std::uint64_t>(priority) & 0xFFu) |
                   (is_fiq ? 0x100ULL : 0ULL) |
                   (is_virtual ? 0x200ULL : 0ULL);
    rec.payload3 = target_el;
    rec.payload_size = 8;
    return rec;
}

SndlRecord make_timer_expiration_record(std::uint16_t core_id, std::uint64_t pc,
                                       std::uint64_t ret_count, std::uint8_t timer_type,
                                       std::uint64_t cval, std::uint64_t counter) noexcept {
    SndlRecord rec{};
    rec.core_id = core_id;
    rec.pc = pc;
    rec.instruction_retirement_count = ret_count;
    rec.event_type = SndlEventType::TimerExpiration;
    rec.boundary_type = SndlBoundaryType::BetweenInstructions;
    rec.payload1 = cval;
    rec.payload2 = counter;
    rec.payload3 = timer_type;
    rec.payload_size = 17;
    return rec;
}

SndlRecord make_counter_read_record(std::uint16_t core_id, std::uint64_t pc,
                                   std::uint64_t ret_count, std::uint16_t sub_ord,
                                   std::uint32_t reg_key, std::uint64_t counter_val,
                                   std::uint64_t frequency) noexcept {
    SndlRecord rec{};
    rec.core_id = core_id;
    rec.pc = pc;
    rec.instruction_retirement_count = ret_count;
    rec.sub_instruction_ordinal = sub_ord;
    rec.event_type = SndlEventType::CounterReadResult;
    rec.boundary_type = SndlBoundaryType::DuringInstruction;
    rec.payload1 = reg_key;
    rec.payload2 = counter_val;
    rec.payload3 = frequency;
    rec.payload_size = 20;
    return rec;
}

SndlRecord make_entropy_record(std::uint16_t core_id, std::uint64_t pc,
                              std::uint64_t ret_count, std::uint16_t sub_ord,
                              std::uint64_t value, unsigned bit_width,
                              std::uint32_t source_id) noexcept {
    SndlRecord rec{};
    rec.core_id = core_id;
    rec.pc = pc;
    rec.instruction_retirement_count = ret_count;
    rec.sub_instruction_ordinal = sub_ord;
    rec.event_type = SndlEventType::EntropyResult;
    rec.boundary_type = SndlBoundaryType::DuringInstruction;
    rec.payload1 = value;
    rec.payload2 = bit_width;
    rec.payload3 = 1; // Success flag
    rec.source_id = source_id;
    rec.payload_size = 8;
    return rec;
}

SndlRecord make_host_input_record(std::uint16_t core_id, std::uint32_t channel_id,
                                 std::uint64_t data_payload, std::size_t length) noexcept {
    SndlRecord rec{};
    rec.core_id = core_id;
    rec.event_type = SndlEventType::HostInput;
    rec.boundary_type = SndlBoundaryType::AsynchronousInput;
    rec.payload1 = channel_id;
    rec.payload2 = data_payload;
    rec.payload3 = static_cast<std::uint64_t>(length);
    rec.source_id = channel_id;
    rec.payload_size = static_cast<std::uint16_t>(length > 8 ? 8 : length);
    return rec;
}

} // namespace winios::sndl
