#include "runtime/userspace_runner.h"

#include <cstring>
#include "runtime/linux_syscall_abi.h"

namespace winios::runtime {

UserspaceExecutionResult UserspaceRunner::run(CPU& cpu, Memory& memory, const UserspaceConfig& config) {
    UserspaceExecutionResult result{};

    if (config.code.empty()) {
        result.error_message = "No code provided in UserspaceConfig";
        return result;
    }

    if (config.entry_point + config.code.size() > memory.size()) {
        result.error_message = "User code extends beyond guest memory bounds";
        return result;
    }

    // 1. Write user code into memory
    for (std::size_t i = 0; i < config.code.size(); ++i) {
        memory.write_u8(config.entry_point + i, config.code[i]);
    }

    // 2. Setup CPU for EL0 userspace execution
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(0); // Userspace EL0
    cpu.set_program_counter(config.entry_point);
    cpu.set_stack_pointer(config.stack_pointer);

    // Vector base for kernel handler: 0x00010000
    // Synchronous exception from lower EL64 routes to VBAR_EL1 + 0x400 (0x00010400)
    constexpr std::uint64_t kVbar = 0x00010000ULL;
    cpu.set_vbar_el1(kVbar);

    // Write a dummy NOP at the kernel vector to satisfy fetch if executed
    if (kVbar + 0x404 <= memory.size()) {
        memory.write_u32(kVbar + 0x400, 0xD503201Fu); // NOP
    }

    // 3. Step execution loop
    while (result.instructions_executed < config.max_instructions) {
        if (cpu.is_halted()) {
            result.exited = true;
            break;
        }

        unsigned prev_el = static_cast<unsigned>(cpu.get_current_el());

        cpu.step(memory);
        result.instructions_executed++;

        // Detect transition from EL0 to EL1 (Syscall entry)
        if (prev_el == 0 && cpu.get_current_el() == 1) {
            std::uint32_t esr = cpu.get_esr_el1();
            std::uint32_t ec = (esr >> 26u) & 0x3Fu;

            if (ec == 0x15u) { // SVC in AArch64
                LinuxSyscallContext ctx = LinuxSyscallContext::capture(cpu);

                if (ctx.syscall_nr == LinuxSyscalls::kExit || ctx.syscall_nr == LinuxSyscalls::kExitGroup) {
                    result.exited = true;
                    result.exit_code = static_cast<std::int32_t>(ctx.args[0]);
                    cpu.set_halted(true);
                    break;
                }

                // Handle syscall and commit return
                LinuxSyscallAbi::handle_synthetic_syscall(cpu, memory, result.console_output);

                // Resume user code at ELR_EL1 in EL0
                cpu.set_program_counter(cpu.get_elr_el1());
                cpu.set_current_el(0);
            } else {
                result.error_message = "Unexpected exception in userspace (ESR=0x" + std::to_string(esr) + ")";
                break;
            }
        }
    }

    if (!result.exited && result.instructions_executed >= config.max_instructions) {
        result.error_message = "Instruction budget exceeded";
    }

    return result;
}

} // namespace winios::runtime
