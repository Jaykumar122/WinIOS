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
        {"_open",                 RuntimeStubKind::BsdSyscall, 5},
        {"_close",                RuntimeStubKind::BsdSyscall, 6},
        {"_read",                 RuntimeStubKind::BsdSyscall, 3},
        {"_write",                RuntimeStubKind::BsdSyscall, 4},
        {"_getpid",               RuntimeStubKind::BsdSyscall, 20},
        {"_getuid",               RuntimeStubKind::BsdSyscall, 24},
        {"_geteuid",              RuntimeStubKind::BsdSyscall, 25},
        {"_getgid",               RuntimeStubKind::BsdSyscall, 47},
        {"_munmap",               RuntimeStubKind::BsdSyscall, 73},
        {"_mmap",                 RuntimeStubKind::BsdSyscall, 197},
        {"_lseek",                RuntimeStubKind::BsdSyscall, 199},
        {"_fstat",                RuntimeStubKind::BsdSyscall, 189},
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

    // Custom: _free (mov x0, #0; ret)
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

    // Custom: _strlen
    {
        RuntimeStubDescriptor strlen_stub;
        strlen_stub.name = "_strlen";
        strlen_stub.kind = RuntimeStubKind::CustomRoutine;
        strlen_stub.number = 0;
        strlen_stub.address = current_addr;
        strlen_stub.code = {
            0xAA0003E1u, // mov x1, x0
            0x38401422u, // ldrb w2, [x1], #1
            0x35FFFFE2u, // cbnz w2, -4 (loop to ldrb)
            0xCB000020u, // sub x0, x1, x0
            0xD1000400u, // sub x0, x0, #1
            0xD65F03C0u  // ret
        };
        exports_[strlen_stub.name] = strlen_stub.address;
        stubs_.push_back(std::move(strlen_stub));
        current_addr += kSlotStride;
    }

    // Custom: _memset
    {
        RuntimeStubDescriptor memset_stub;
        memset_stub.name = "_memset";
        memset_stub.kind = RuntimeStubKind::CustomRoutine;
        memset_stub.number = 0;
        memset_stub.address = current_addr;
        memset_stub.code = {
            0xAA0003E3u, // mov x3, x0
            0xB4000082u, // cbz x2, +16 (ret)
            0x38001461u, // strb w1, [x3], #1
            0xD1000442u, // sub x2, x2, #1
            0x35FFFFC2u, // cbnz x2, -8 (loop to strb)
            0xD65F03C0u  // ret
        };
        exports_[memset_stub.name] = memset_stub.address;
        stubs_.push_back(std::move(memset_stub));
        current_addr += kSlotStride;
    }

    // Custom: _memcpy and _memmove
    {
        RuntimeStubDescriptor memcpy_stub;
        memcpy_stub.name = "_memcpy";
        memcpy_stub.kind = RuntimeStubKind::CustomRoutine;
        memcpy_stub.number = 0;
        memcpy_stub.address = current_addr;
        memcpy_stub.code = {
            0xAA0003E3u, // mov x3, x0
            0xB40000A2u, // cbz x2, +20 (ret)
            0x38401424u, // ldrb w4, [x1], #1
            0x38001464u, // strb w4, [x3], #1
            0xD1000442u, // sub x2, x2, #1
            0x35FFFFA2u, // cbnz x2, -12 (loop to ldrb)
            0xD65F03C0u  // ret
        };
        exports_["_memcpy"] = memcpy_stub.address;
        exports_["_memmove"] = memcpy_stub.address;
        stubs_.push_back(std::move(memcpy_stub));
        current_addr += kSlotStride;
    }

    // Custom: _strcmp
    {
        RuntimeStubDescriptor strcmp_stub;
        strcmp_stub.name = "_strcmp";
        strcmp_stub.kind = RuntimeStubKind::CustomRoutine;
        strcmp_stub.number = 0;
        strcmp_stub.address = current_addr;
        strcmp_stub.code = {
            0x38401402u, // ldrb w2, [x0], #1
            0x38401423u, // ldrb w3, [x1], #1
            0x34000062u, // cbz w2, +12 (done)
            0x6B03005Fu, // cmp w2, w3
            0x54FFFF80u, // b.eq -16 (loop)
            0x4B030040u, // sub w0, w2, w3
            0xD65F03C0u  // ret
        };
        exports_[strcmp_stub.name] = strcmp_stub.address;
        stubs_.push_back(std::move(strcmp_stub));
        current_addr += kSlotStride;
    }

    // Custom: _abort
    {
        RuntimeStubDescriptor abort_stub;
        abort_stub.name = "_abort";
        abort_stub.kind = RuntimeStubKind::CustomRoutine;
        abort_stub.number = 0;
        abort_stub.address = current_addr;
        abort_stub.code = {
            0xD28010C0u, // movz x0, #134 (SIGABRT)
            0xD2800030u, // movz x16, #1 (sys_exit)
            0xD4001001u, // svc #0x80
            0xD65F03C0u  // ret
        };
        exports_[abort_stub.name] = abort_stub.address;
        stubs_.push_back(std::move(abort_stub));
        current_addr += kSlotStride;
    }

    // Custom: ___tlv_bootstrap and __tlv_bootstrap
    {
        RuntimeStubDescriptor tlv_stub;
        tlv_stub.name = "___tlv_bootstrap";
        tlv_stub.kind = RuntimeStubKind::CustomRoutine;
        tlv_stub.number = 0;
        tlv_stub.address = current_addr;
        tlv_stub.code = {
            0xF9400400u, // ldr x0, [x0, #8]
            0xD65F03C0u  // ret
        };
        exports_["___tlv_bootstrap"] = tlv_stub.address;
        exports_["__tlv_bootstrap"] = tlv_stub.address;
        stubs_.push_back(std::move(tlv_stub));
        current_addr += kSlotStride;
    }

    // Custom: ___stack_chk_fail
    {
        RuntimeStubDescriptor chk_stub;
        chk_stub.name = "___stack_chk_fail";
        chk_stub.kind = RuntimeStubKind::CustomRoutine;
        chk_stub.number = 0;
        chk_stub.address = current_addr;
        chk_stub.code = {
            0xD2800020u, // movz x0, #1
            0xD2800030u, // movz x16, #1 (exit)
            0xD4001001u, // svc #0x80
            0xD65F03C0u  // ret
        };
        exports_[chk_stub.name] = chk_stub.address;
        stubs_.push_back(std::move(chk_stub));
        current_addr += kSlotStride;
    }

    // Data storage: errno and stack canary
    errno_address_ = current_addr;
    stack_guard_address_ = current_addr + 0x10;
    exports_["___stack_chk_guard"] = stack_guard_address_;
    current_addr += kSlotStride;

    // Custom: ___error (Darwin errno pointer)
    {
        RuntimeStubDescriptor err_stub;
        err_stub.name = "___error";
        err_stub.kind = RuntimeStubKind::CustomRoutine;
        err_stub.number = 0;
        err_stub.address = current_addr;
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

    if (errno_address_ != 0) {
        std::size_t off = translate_vaddr(errno_address_, memory.size(), base_vaddr_);
        if (off + 4 <= memory.size()) {
            memory.write_u32(off, 0);
        }
    }

    if (stack_guard_address_ != 0) {
        std::size_t off = translate_vaddr(stack_guard_address_, memory.size(), base_vaddr_);
        if (off + 8 <= memory.size()) {
            memory.write_u64(off, stack_guard_value_);
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
