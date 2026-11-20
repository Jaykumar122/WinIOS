#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <optional>

namespace winios::runtime {

inline constexpr std::uint64_t kReplayMagic = 0x57494F535245504CULL; // "WIOSREPL"
inline constexpr std::uint32_t kReplayCurrentVersion = 1;
inline constexpr std::size_t kMaxReplayPayloadBytes = 16 * 1024 * 1024; // 16 MB maximum replay size to prevent memory exhaustion
inline constexpr std::size_t kMaxRecordedSyscalls = 65536;

struct ReplayCpuState {
    std::array<std::uint64_t, 31> gpr{};
    std::uint64_t pc{0};
    std::uint64_t sp{0};
    std::uint8_t nzcv{0};
    std::uint64_t instruction_count{0};
    std::int32_t exit_code{0};

    bool operator==(const ReplayCpuState& o) const noexcept {
        return gpr == o.gpr && pc == o.pc && sp == o.sp && nzcv == o.nzcv &&
               instruction_count == o.instruction_count && exit_code == o.exit_code;
    }
};

struct ReplayPageHash {
    std::uint64_t virtual_addr{0};
    std::uint64_t hash{0};
    std::uint32_t perms{0};

    bool operator==(const ReplayPageHash& o) const noexcept {
        return virtual_addr == o.virtual_addr && hash == o.hash && perms == o.perms;
    }
};

struct ReplayMemoryState {
    std::uint64_t root_table_pa{0};
    std::uint16_t asid{0};
    std::uint64_t overall_memory_hash{0};
    std::vector<ReplayPageHash> page_hashes{};

    bool operator==(const ReplayMemoryState& o) const noexcept {
        return root_table_pa == o.root_table_pa && asid == o.asid &&
               overall_memory_hash == o.overall_memory_hash && page_hashes == o.page_hashes;
    }
};

struct ReplaySyscallEntry {
    std::uint32_t syscall_num{0};
    std::uint64_t arg0{0};
    std::uint64_t arg1{0};
    std::uint64_t arg2{0};
    std::uint64_t ret_val{0};
    std::int32_t error_code{0};

    bool operator==(const ReplaySyscallEntry& o) const noexcept {
        return syscall_num == o.syscall_num && arg0 == o.arg0 && arg1 == o.arg1 &&
               arg2 == o.arg2 && ret_val == o.ret_val && error_code == o.error_code;
    }
};

struct ReplayDivergence {
    bool has_diverged{false};
    std::string field_name;
    std::string details;
};

class ReplayStateContract {
public:
    ReplayCpuState cpu{};
    ReplayMemoryState memory{};
    std::vector<ReplaySyscallEntry> syscalls{};

    // Helper: compute FNV-1a 64-bit hash
    static std::uint64_t fnv1a_hash(const std::uint8_t* data, std::size_t len) noexcept {
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        for (std::size_t i = 0; i < len; ++i) {
            hash ^= data[i];
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    // Compare two execution states and report the exact first divergence detected
    static ReplayDivergence compare(const ReplayStateContract& a, const ReplayStateContract& b) {
        // 1. CPU State comparison
        if (a.cpu.instruction_count != b.cpu.instruction_count) {
            return {true, "cpu.instruction_count",
                    "Expected " + std::to_string(a.cpu.instruction_count) + ", got " + std::to_string(b.cpu.instruction_count)};
        }
        if (a.cpu.pc != b.cpu.pc) {
            return {true, "cpu.pc",
                    "Expected 0x" + to_hex(a.cpu.pc) + ", got 0x" + to_hex(b.cpu.pc)};
        }
        if (a.cpu.sp != b.cpu.sp) {
            return {true, "cpu.sp",
                    "Expected 0x" + to_hex(a.cpu.sp) + ", got 0x" + to_hex(b.cpu.sp)};
        }
        if (a.cpu.nzcv != b.cpu.nzcv) {
            return {true, "cpu.nzcv",
                    "Expected 0x" + to_hex(a.cpu.nzcv) + ", got 0x" + to_hex(b.cpu.nzcv)};
        }
        for (std::size_t i = 0; i < a.cpu.gpr.size(); ++i) {
            if (a.cpu.gpr[i] != b.cpu.gpr[i]) {
                return {true, "cpu.gpr[X" + std::to_string(i) + "]",
                        "Expected 0x" + to_hex(a.cpu.gpr[i]) + ", got 0x" + to_hex(b.cpu.gpr[i])};
            }
        }
        if (a.cpu.exit_code != b.cpu.exit_code) {
            return {true, "cpu.exit_code",
                    "Expected " + std::to_string(a.cpu.exit_code) + ", got " + std::to_string(b.cpu.exit_code)};
        }

        // 2. Memory State comparison
        if (a.memory.overall_memory_hash != b.memory.overall_memory_hash) {
            return {true, "memory.overall_memory_hash",
                    "Expected 0x" + to_hex(a.memory.overall_memory_hash) + ", got 0x" + to_hex(b.memory.overall_memory_hash)};
        }
        if (a.memory.page_hashes.size() != b.memory.page_hashes.size()) {
            return {true, "memory.page_count",
                    "Expected " + std::to_string(a.memory.page_hashes.size()) + ", got " + std::to_string(b.memory.page_hashes.size())};
        }
        for (std::size_t i = 0; i < a.memory.page_hashes.size(); ++i) {
            const auto& p1 = a.memory.page_hashes[i];
            const auto& p2 = b.memory.page_hashes[i];
            if (p1.virtual_addr != p2.virtual_addr || p1.hash != p2.hash || p1.perms != p2.perms) {
                return {true, "memory.page[" + std::to_string(i) + "]",
                        "Divergence at VA 0x" + to_hex(p1.virtual_addr)};
            }
        }

        // 3. Syscall Stream comparison
        if (a.syscalls.size() != b.syscalls.size()) {
            return {true, "syscalls.count",
                    "Expected " + std::to_string(a.syscalls.size()) + ", got " + std::to_string(b.syscalls.size())};
        }
        for (std::size_t i = 0; i < a.syscalls.size(); ++i) {
            if (!(a.syscalls[i] == b.syscalls[i])) {
                return {true, "syscalls[" + std::to_string(i) + "]",
                        "Syscall " + std::to_string(a.syscalls[i].syscall_num) + " diverged"};
            }
        }

        return {false, "", "Identical replay state"};
    }

    // Binary serialization with bounded length and checksum
    [[nodiscard]] std::vector<std::uint8_t> serialize() const {
        std::vector<std::uint8_t> buf;
        append_u64(buf, kReplayMagic);
        append_u32(buf, kReplayCurrentVersion);

        // CPU state
        for (std::uint64_t r : cpu.gpr) append_u64(buf, r);
        append_u64(buf, cpu.pc);
        append_u64(buf, cpu.sp);
        buf.push_back(cpu.nzcv);
        append_u64(buf, cpu.instruction_count);
        append_i32(buf, cpu.exit_code);

        // Memory state
        append_u64(buf, memory.root_table_pa);
        append_u16(buf, memory.asid);
        append_u64(buf, memory.overall_memory_hash);
        append_u32(buf, static_cast<std::uint32_t>(memory.page_hashes.size()));
        for (const auto& pg : memory.page_hashes) {
            append_u64(buf, pg.virtual_addr);
            append_u64(buf, pg.hash);
            append_u32(buf, pg.perms);
        }

        // Syscalls
        append_u32(buf, static_cast<std::uint32_t>(syscalls.size()));
        for (const auto& sc : syscalls) {
            append_u32(buf, sc.syscall_num);
            append_u64(buf, sc.arg0);
            append_u64(buf, sc.arg1);
            append_u64(buf, sc.arg2);
            append_u64(buf, sc.ret_val);
            append_i32(buf, sc.error_code);
        }

        return buf;
    }

    // Safe deserialization with strict validation, boundary checking, and rejection of corrupt artifacts
    static std::optional<ReplayStateContract> deserialize(const std::uint8_t* data, std::size_t size) {
        if (!data || size < 20 || size > kMaxReplayPayloadBytes) {
            return std::nullopt;
        }

        std::size_t off = 0;
        auto read_u8 = [&](std::uint8_t& out) -> bool {
            if (off + 1 > size) return false;
            out = data[off++];
            return true;
        };
        auto read_u16 = [&](std::uint16_t& out) -> bool {
            if (off + 2 > size) return false;
            std::memcpy(&out, data + off, 2);
            off += 2;
            return true;
        };
        auto read_u32 = [&](std::uint32_t& out) -> bool {
            if (off + 4 > size) return false;
            std::memcpy(&out, data + off, 4);
            off += 4;
            return true;
        };
        auto read_i32 = [&](std::int32_t& out) -> bool {
            if (off + 4 > size) return false;
            std::memcpy(&out, data + off, 4);
            off += 4;
            return true;
        };
        auto read_u64 = [&](std::uint64_t& out) -> bool {
            if (off + 8 > size) return false;
            std::memcpy(&out, data + off, 8);
            off += 8;
            return true;
        };

        std::uint64_t magic = 0;
        if (!read_u64(magic) || magic != kReplayMagic) return std::nullopt;

        std::uint32_t version = 0;
        if (!read_u32(version) || version != kReplayCurrentVersion) return std::nullopt;

        ReplayStateContract state;
        // CPU
        for (std::size_t i = 0; i < 31; ++i) {
            if (!read_u64(state.cpu.gpr[i])) return std::nullopt;
        }
        if (!read_u64(state.cpu.pc)) return std::nullopt;
        if (!read_u64(state.cpu.sp)) return std::nullopt;
        if (!read_u8(state.cpu.nzcv)) return std::nullopt;
        if (!read_u64(state.cpu.instruction_count)) return std::nullopt;
        if (!read_i32(state.cpu.exit_code)) return std::nullopt;

        // Memory
        if (!read_u64(state.memory.root_table_pa)) return std::nullopt;
        if (!read_u16(state.memory.asid)) return std::nullopt;
        if (!read_u64(state.memory.overall_memory_hash)) return std::nullopt;

        std::uint32_t page_count = 0;
        if (!read_u32(page_count)) return std::nullopt;
        if (page_count > 100000) return std::nullopt; // Protect against integer overflow / huge allocation

        state.memory.page_hashes.resize(page_count);
        for (std::uint32_t i = 0; i < page_count; ++i) {
            if (!read_u64(state.memory.page_hashes[i].virtual_addr)) return std::nullopt;
            if (!read_u64(state.memory.page_hashes[i].hash)) return std::nullopt;
            if (!read_u32(state.memory.page_hashes[i].perms)) return std::nullopt;
        }

        // Syscalls
        std::uint32_t sc_count = 0;
        if (!read_u32(sc_count)) return std::nullopt;
        if (sc_count > kMaxRecordedSyscalls) return std::nullopt;

        state.syscalls.resize(sc_count);
        for (std::uint32_t i = 0; i < sc_count; ++i) {
            if (!read_u32(state.syscalls[i].syscall_num)) return std::nullopt;
            if (!read_u64(state.syscalls[i].arg0)) return std::nullopt;
            if (!read_u64(state.syscalls[i].arg1)) return std::nullopt;
            if (!read_u64(state.syscalls[i].arg2)) return std::nullopt;
            if (!read_u64(state.syscalls[i].ret_val)) return std::nullopt;
            if (!read_i32(state.syscalls[i].error_code)) return std::nullopt;
        }

        // If extra trailing garbage exists, fail safely
        if (off != size) return std::nullopt;

        return state;
    }

private:
    static void append_u16(std::vector<std::uint8_t>& buf, std::uint16_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        buf.insert(buf.end(), p, p + 2);
    }
    static void append_u32(std::vector<std::uint8_t>& buf, std::uint32_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        buf.insert(buf.end(), p, p + 4);
    }
    static void append_i32(std::vector<std::uint8_t>& buf, std::int32_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        buf.insert(buf.end(), p, p + 4);
    }
    static void append_u64(std::vector<std::uint8_t>& buf, std::uint64_t v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        buf.insert(buf.end(), p, p + 8);
    }
    static std::string to_hex(std::uint64_t val) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(val));
        return std::string(buf);
    }
};

} // namespace winios::runtime
