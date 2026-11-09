#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "loader/dyld_engine.h"
#include "memory/memory.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// Darwin Runtime Stub Identifier
// -----------------------------------------------------------------------------
enum class RuntimeStubKind {
    BsdSyscall,
    MachTrap,
    CustomRoutine
};

struct RuntimeStubDescriptor {
    std::string     name;
    RuntimeStubKind kind{RuntimeStubKind::BsdSyscall};
    std::int64_t    number{0}; // Syscall/trap number
    std::uint64_t   address{0};
    std::vector<std::uint32_t> code;
};

// -----------------------------------------------------------------------------
// DarwinRuntimeLibrary: Generates and Stages libSystem.B.dylib Guest Stubs
// -----------------------------------------------------------------------------
class DarwinRuntimeLibrary {
public:
    explicit DarwinRuntimeLibrary(std::uint64_t base_vaddr = 0x0000000180000000ULL);
    ~DarwinRuntimeLibrary() = default;

    // Stage runtime ARM64 stubs into guest physical/virtual memory
    bool stage_into_memory(Memory& memory, std::string& out_error);

    // Register all runtime stubs into DyldEngine as libSystem.B.dylib
    void register_with_dyld(loader::DyldEngine& dyld);

    // Lookup stub by exported name
    [[nodiscard]] const RuntimeStubDescriptor* find_stub(std::string_view name) const noexcept;

    // Lookup stub by guest virtual address
    [[nodiscard]] const RuntimeStubDescriptor* find_stub_at(std::uint64_t vaddr) const noexcept;

    // Base address where stubs are placed
    [[nodiscard]] std::uint64_t base_vaddr() const noexcept { return base_vaddr_; }

    // Total size occupied by runtime stubs
    [[nodiscard]] std::size_t total_size() const noexcept { return stubs_.size() * 0x40; }

    // Export map: symbol name -> virtual address
    [[nodiscard]] const std::map<std::string, std::uint64_t>& exports() const noexcept {
        return exports_;
    }

private:
    std::uint64_t                         base_vaddr_;
    std::vector<RuntimeStubDescriptor>    stubs_;
    std::map<std::string, std::uint64_t>  exports_;

    void initialize_stub_catalog();
    static std::vector<std::uint32_t> emit_bsd_stub(std::uint16_t syscall_num);
    static std::vector<std::uint32_t> emit_mach_stub(std::int16_t trap_num);
};

} // namespace winios::runtime
