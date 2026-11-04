#pragma once

#include "sndl/sndl_v2_types.h"
#include <cstddef>
#include <cstdint>

namespace winios::sndl {

// -----------------------------------------------------------------------------
// SNDL v2 Header Encoding / Decoding
// -----------------------------------------------------------------------------
void encode_sndl_v2_header(const WSndlV2Header& hdr, std::uint8_t* dst) noexcept;
[[nodiscard]] SndlV2Result decode_sndl_v2_header(const std::uint8_t* src, std::size_t size, WSndlV2Header& out_hdr) noexcept;

// -----------------------------------------------------------------------------
// SNDL v2 Block Header Encoding / Decoding
// -----------------------------------------------------------------------------
void encode_sndl_v2_block_header(const WSndlV2BlockHeader& blk, std::uint8_t* dst) noexcept;
[[nodiscard]] SndlV2Result decode_sndl_v2_block_header(const std::uint8_t* src, std::size_t size, WSndlV2BlockHeader& out_blk) noexcept;

// -----------------------------------------------------------------------------
// SNDL v2 Record Encoding / Decoding
// -----------------------------------------------------------------------------
void encode_sndl_v2_record(const SndlV2Record& rec, std::uint8_t* dst) noexcept;
[[nodiscard]] SndlV2Result decode_sndl_v2_record(const std::uint8_t* src, std::size_t size, SndlV2Record& out_rec) noexcept;

// -----------------------------------------------------------------------------
// SNDL v2 Footer Encoding / Decoding
// -----------------------------------------------------------------------------
void encode_sndl_v2_footer(const WSndlV2Footer& ftr, std::uint8_t* dst) noexcept;
[[nodiscard]] SndlV2Result decode_sndl_v2_footer(const std::uint8_t* src, std::size_t size, WSndlV2Footer& out_ftr) noexcept;

} // namespace winios::sndl
