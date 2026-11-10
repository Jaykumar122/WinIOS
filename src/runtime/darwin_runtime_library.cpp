#include "runtime/darwin_runtime_library.h"

#include <algorithm>
#include <cstring>

namespace winios::runtime {

namespace {

inline std::size_t translate_vaddr(std::uint64_t vaddr, std::size_t mem_size, std::uint64_t base) noexcept {
    if (vaddr < mem_size) {
        return static_cast<std::size_t>(vaddr);
    }
    if (base > 0 && vaddr >= base) {
        std::uint64_t rel = vaddr - base;
        if (rel < mem_size) {
            return static_cast<std::size_t>(rel);
        }
    }
    return static_cast<std::size_t>(vaddr % mem_size);
}

} // anonymous namespace

DarwinRuntimeLibrary::DarwinRuntimeLibrary(std::uint64_t base_vaddr)
    : base_vaddr_(base_vaddr) {
    initialize_stub_catalog();
}

std::vector<std::uint32_t> DarwinRuntimeLibrary::emit_bsd_stub(std::uint16_t syscall_num) {
    // movz x16, #syscall_num
    std::uint32_t movz_x16 = 0xD2800010u | ((static_cast<std::uint32_t>(syscall_num) & 0xFFFFu) << 5);
    // svc #0x80 (Darwin BSD syscall trap)
    std::uint32_t svc_80   = 0xD4001001u;
    // ret
    std::uint32_t ret_ins  = 0xD65F03C0u;

    return {movz_x16, svc_80, ret_ins};
}

std::vector<std::uint32_t> DarwinRuntimeLibrary::emit_mach_stub(std::int16_t trap_num) {
    // For negative trap numbers (e.g. -26): ~(-26) = 25
    std::uint16_t inv = static_cast<std::uint16_t>(~trap_num);
    // movn x16, #inv
    std::uint32_t movn_x16 = 0x92800010u | ((static_cast<std::uint32_t>(inv) & 0xFFFFu) << 5);
    // svc #0x81 (Darwin Mach trap)
    std::uint32_t svc_81   = 0xD4001021u;
    // ret
    std::uint32_t ret_ins  = 0xD65F03C0u;

    return {movn_x16, svc_81, ret_ins};
}

void DarwinRuntimeLibrary::initialize_stub_catalog() {
    stubs_.clear();
    exports_.clear();

    struct StubDef {
        const char*     name;
        RuntimeStubKind kind;
        std::int64_t    num;
    };

    static const StubDef kDefs[] = {
        {"_exit",                 RuntimeStubKind::BsdSyscall, 1},
        {"_read",                 RuntimeStubKind::BsdSyscall, 3},
        {"_write",                RuntimeStubKind::BsdSyscall, 4},
        {"_getpid",               RuntimeStubKind::BsdSyscall, 20},
        {"_munmap",               RuntimeStubKind::BsdSyscall, 73},
        {"_mmap",                 RuntimeStubKind::BsdSyscall, 197},
        {"_kdebug_trace",         RuntimeStubKind::BsdSyscall, 180},
        {"_mach_msg_trap",        RuntimeStubKind::MachTrap,   -31},
        {"_mach_reply_port",      RuntimeStubKind::MachTrap,   -26},
        {"_mach_task_self",       RuntimeStubKind::MachTrap,   -28},
        {"_task_self_trap",       RuntimeStubKind::MachTrap,   -28},
        {"_semaphore_signal_trap",RuntimeStubKind::MachTrap,   -33},
        {"_semaphore_wait_trap",  RuntimeStubKind::MachTrap,   -36},
        {"_swtch_pri",            RuntimeStubKind::MachTrap,   -59},
        {"_swtch",                RuntimeStubKind::MachTrap,   -60}
    };

    constexpr std::uint64_t kSlotStride = 0x40; // 64 bytes per stub slot
    std::uint64_t current_addr = base_vaddr_;

    for (const auto& d : kDefs) {
        RuntimeStubDescriptor stub;
        stub.name = d.name;
        stub.kind = d.kind;
        stub.number = d.num;
        stub.address = current_addr;

        if (d.kind == RuntimeStubKind::BsdSyscall) {
            stub.code = emit_bsd_stub(static_cast<std::uint16_t>(d.num));
        } else if (d.kind == RuntimeStubKind::MachTrap) {
            stub.code = emit_mach_stub(static_cast<std::int16_t>(d.num));
        }

        exports_[stub.name] = stub.address;
        stubs_.push_back(std::move(stub));
        current_addr += kSlotStride;
    }

    // 16. Custom: _free (mov x0, #0; ret)
    {
        RuntimeStubDescriptor free_stub;
        free_stub.name = "_free";
        free_stub.kind = RuntimeStubKind::CustomRoutine;
        free_stub.number = 0;
        free_stub.address = current_addr;
        free_stub.code = {0xD2800000u, 0xD65F03C0u}; // movz x0, #0; ret
        exports_[free_stub.name] = free_stub.address;
        stubs_.push_back(std::move(free_stub));
        current_addr += kSlotStride;
    }

    // 17. Custom: ___error (Darwin errno pointer)
    {
        RuntimeStubDescriptor err_stub;
        err_stub.name = "___error";
        err_stub.kind = RuntimeStubKind::CustomRoutine;
        err_stub.number = 0;
        err_stub.address = current_addr;
        // movz x0, #0xFF00, lsl #0; movk x0, #0x1800, lsl #16 -> pointer to errno storage
        // movz x0, #0x1000; ret
        err_stub.code = {0xD2820000u, 0xD65F03C0u}; // movz x0, #0x1000; ret
        exports_[err_stub.name] = err_stub.address;
        stubs_.push_back(std::move(err_stub));
        current_addr += kSlotStride;
    }
}

bool DarwinRuntimeLibrary::stage_into_memory(Memory& memory, std::string& out_error) {
    for (const auto& stub : stubs_) {
        std::size_t mem_off = translate_vaddr(stub.address, memory.size(), base_vaddr_);
        std::size_t code_bytes = stub.code.size() * sizeof(std::uint32_t);

        if (mem_off + code_bytes > memory.size()) {
            out_error = "Stub " + stub.name + " exceeds guest memory bounds";
            return false;
        }

        for (std::size_t i = 0; i < stub.code.size(); ++i) {
            memory.write_u32(mem_off + (i * sizeof(std::uint32_t)), stub.code[i]);
        }
    }
    return true;
}

void DarwinRuntimeLibrary::register_with_dyld(loader::DyldEngine& dyld) {
    loader::DyldLoadedModule libsystem;
    libsystem.name = "libSystem.B.dylib";
    libsystem.install_name = "/usr/lib/libSystem.B.dylib";
    libsystem.base_address = base_vaddr_;
    libsystem.slide = 0;
    libsystem.exports = exports_;

    dyld.register_module(std::move(libsystem));
}

const RuntimeStubDescriptor* DarwinRuntimeLibrary::find_stub(std::string_view name) const noexcept {
    for (const auto& s : stubs_) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

const RuntimeStubDescriptor* DarwinRuntimeLibrary::find_stub_at(std::uint64_t vaddr) const noexcept {
    constexpr std::uint64_t kSlotStride = 0x40;
    for (const auto& s : stubs_) {
        if (vaddr >= s.address && vaddr < s.address + kSlotStride) {
            return &s;
        }
    }
    return nullptr;
}

} // namespace winios::runtime
