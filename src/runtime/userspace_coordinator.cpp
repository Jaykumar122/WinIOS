#include "runtime/userspace_coordinator.h"
#include "loader/guest_dynamic_loader.h"
#include "loader/macho_dependency_graph.h"

#include <iostream>

namespace winios::runtime {

UserspaceLaunchResult UserspaceCoordinator::launch_and_run(
    const std::vector<std::uint8_t>& macho_binary,
    const std::string& process_name,
    const std::vector<std::string>& argv,
    const std::vector<std::string>& envp,
    std::size_t max_steps) {
    UserspaceLaunchResult res{};

    // 1. Guest memory (8 MiB DRAM)
    Memory mem(0x800000);

    // 2. Physical Page Manager
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    // 3. Kernel thread manager and deterministic scheduler
    kernel::KernelThreadManager thread_mgr;
    kernel::PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    // 4. Virtual console and syscall dispatcher
    kernel::VirtualConsole console;
    kernel::UserSyscallDispatcher dispatcher(console);

    // 5. BSD Process Manager
    kernel::BsdProcessManager proc_mgr;
    proc_mgr.initialize();

    // 6. Allocate root L2 page table for user address space
    const std::uint64_t root_l2_pa = ppm.allocate_page();
    if (root_l2_pa == 0) {
        res.error_message = "Failed to allocate root L2 page table for user address space";
        return res;
    }

    // 7. Create UserAddressSpace
    memory::UserAddressSpace uas(mem, ppm, root_l2_pa, process_name + "_vm");

    // 8. Load Mach-O executable
    auto loaded_img = loader::MachoUserLoader::load_image(macho_binary, uas, ppm, mem);
    if (!loaded_img.valid) {
        res.error_message = "Mach-O load failed: " + loaded_img.error_message;
        return res;
    }

    // 9. Construct user stack
    kernel::UserStackConfig stack_cfg;
    stack_cfg.argv = argv;
    stack_cfg.envp = envp;
    stack_cfg.stack_size = std::min(loaded_img.stack_size, 0x10000ULL);
    auto stack_res = kernel::UserStackBuilder::build_stack(uas, ppm, mem, stack_cfg);
    if (!stack_res.success) {
        res.error_message = "User stack construction failed";
        return res;
    }

    // 10. Create BSD Process and User Thread
    auto* proc = proc_mgr.create_process(process_name, 0);
    if (!proc) {
        res.error_message = "Failed to allocate BSD process";
        return res;
    }
    res.pid = proc->pid();

    auto* thread = thread_mgr.create_thread(
        process_name + "_main",
        loaded_img.entry_point,
        proc->pid(),
        stack_res.final_sp_el0,
        stack_cfg.stack_size,
        16,
        true);
    if (!thread) {
        res.error_message = "Failed to create user main thread";
        return res;
    }

    // Initialize user thread architectural register state
    thread->tcb().pc = loaded_img.entry_point;
    thread->tcb().sp_el0 = stack_res.final_sp_el0;
    thread->tcb().spsr_el1 = 0x00ULL; // Mode EL0t, DAIF unmasked
    thread->tcb().gpr[0] = stack_res.argc;
    thread->tcb().gpr[1] = stack_res.argv_va;
    thread->tcb().gpr[2] = stack_res.envp_va;

    proc->add_thread(thread);
    sched.enqueue_thread(thread);

    // 11. Configure virtual CPU
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0x00300000ULL); // FPEN=11

    const std::uint64_t vbar_pa = ppm.allocate_page();
    const std::uint64_t vbar_va = 0x80000ULL;
    cpu.set_vbar_el1(vbar_va);
    mem.write_u32(static_cast<std::size_t>(vbar_pa + 0x400u), 0xD503201Fu); // NOP
    uas.map_user_page(vbar_va, vbar_pa, memory::PagePermissions::UserExecute);

    // T0SZ=28 (36-bit VA), TG0=0b10 (16KB), SH0=0b11, ORGN0=0b01, IRGN0=0b01
    const std::uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(uas.root_table_pa());
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u));

    // Dispatch the user thread onto the virtual CPU
    if (!sched.dispatch_next(cpu)) {
        res.error_message = "Failed to dispatch user thread to CPU";
        return res;
    }

    // Ensure CPU is executing at EL0 with thread's initial state
    cpu.set_current_el(0);
    cpu.set_program_counter(loaded_img.entry_point);
    cpu.set_sp_el0(stack_res.final_sp_el0);

    // 12. Execution Loop
    std::size_t step = 0;
    while (step < max_steps) {
        if (proc->is_terminated()) {
            break;
        }

        const std::uint64_t cur_pc = cpu.get_program_counter();
        const unsigned cur_el = static_cast<unsigned>(cpu.get_current_el());

        // Translate instruction PC to fetch instruction for tracing
        const std::uint64_t insn_pa = uas.translate(cur_pc);
        std::uint32_t insn = 0;
        if (insn_pa != 0 && insn_pa + 4 <= mem.size()) {
            insn = mem.read_u32(static_cast<std::size_t>(insn_pa));
        }

        UserspaceTraceStep trace_step{};
        trace_step.step_index = step;
        trace_step.pc = cur_pc;
        trace_step.current_el = cur_el;
        trace_step.instruction = insn;

        // Step CPU execution
        cpu.step(mem);
        ++res.instructions_retired;

        // Check if SVC exception was raised (transitioned from EL0 to EL1 with EC=0x15)
        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            trace_step.is_syscall = true;
            trace_step.syscall_num = (cpu.read_register(16) != 0) ? cpu.read_register(16) : cpu.read_register(0);

            // Dispatch syscall in kernel
            dispatcher.handle_syscall(cpu, *proc, uas, sched);
            ++res.syscalls_handled;
            trace_step.syscall_ret = cpu.read_register(0);

            if (!proc->is_terminated()) {
                // Return from exception back to EL0 userspace (simulating kernel ERET)
                cpu.set_current_el(0);
                cpu.set_program_counter(cpu.get_elr_el1());
            }
        }

        res.trace.push_back(trace_step);
        ++step;

        if (cpu.is_waiting_for_interrupt()) {
            break;
        }
    }

    res.success = (res.instructions_retired > 0 && res.syscalls_handled > 0);
    res.exit_code = proc->exit_code();
    res.console_output = console.get_output();
    return res;
}

UserspaceLaunchResult UserspaceCoordinator::launch_and_run_dynamic(
    const std::vector<std::uint8_t>& macho_binary,
    const std::unordered_map<std::string, std::vector<std::uint8_t>>& dylib_registry,
    const std::string& process_name,
    const std::vector<std::string>& argv,
    const std::vector<std::string>& envp,
    std::size_t max_steps) {
    UserspaceLaunchResult res{};

    // 1. Dependency Resolution
    loader::MachoDependencyGraph graph;
    std::string err;

    for (const auto& [name, bin] : dylib_registry) {
        if (!graph.add_image(name, bin, &err)) {
            res.error_message = "Failed to register dependency '" + name + "': " + err;
            return res;
        }
    }

    if (!graph.add_image(process_name, macho_binary, &err)) {
        res.error_message = "Failed to register root executable '" + process_name + "': " + err;
        return res;
    }

    std::vector<std::string> load_order;
    if (!graph.resolve_load_order(process_name, load_order, &err)) {
        res.error_message = "Dependency resolution failed: " + err;
        return res;
    }

    // 2. Hardware and Memory Initialization (8 MiB DRAM)
    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    kernel::KernelThreadManager thread_mgr;
    kernel::PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    kernel::VirtualConsole console;
    kernel::UserSyscallDispatcher dispatcher(console);

    kernel::BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    dispatcher.set_process_manager(&proc_mgr);

    // 3. User Address Space
    const std::uint64_t root_l2_pa = ppm.allocate_page();
    if (root_l2_pa == 0) {
        res.error_message = "Failed to allocate root L2 page table";
        return res;
    }

    memory::UserAddressSpace uas(mem, ppm, root_l2_pa, process_name + "_vm");
    loader::GuestDynamicLoader dyn_loader(uas, ppm, mem);

    // 4. Load Dependencies in Topological Order
    for (const auto& dep_name : load_order) {
        if (dep_name == process_name) continue; // Skip root executable here
        auto it = dylib_registry.find(dep_name);
        if (it == dylib_registry.end()) {
            res.error_message = "Missing registered binary for dependency: '" + dep_name + "'";
            return res;
        }
        if (!dyn_loader.load_library(dep_name, it->second, nullptr, &err)) {
            res.error_message = "Failed to load dynamic library '" + dep_name + "': " + err;
            return res;
        }
    }

    // 5. Load Main Executable Image
    auto loaded_img = loader::MachoUserLoader::load_image(macho_binary, uas, ppm, mem);
    if (!loaded_img.valid) {
        res.error_message = "Mach-O load failed: " + loaded_img.error_message;
        return res;
    }

    // 6. Bind Dynamic Symbols
    if (!dyn_loader.resolve_and_bind_symbols(macho_binary, 0, &err)) {
        res.error_message = "Symbol binding failed: " + err;
        return res;
    }

    // 7. Construct User Stack
    kernel::UserStackConfig stack_cfg;
    stack_cfg.argv = argv;
    stack_cfg.envp = envp;
    stack_cfg.stack_size = std::min(loaded_img.stack_size, 0x10000ULL);
    auto stack_res = kernel::UserStackBuilder::build_stack(uas, ppm, mem, stack_cfg);
    if (!stack_res.success) {
        res.error_message = "User stack construction failed";
        return res;
    }

    // 8. Create Process and Thread
    auto* proc = proc_mgr.create_process(process_name, 0);
    if (!proc) {
        res.error_message = "Failed to allocate BSD process";
        return res;
    }
    res.pid = proc->pid();

    auto* thread = thread_mgr.create_thread(
        process_name + "_main",
        loaded_img.entry_point,
        proc->pid(),
        stack_res.final_sp_el0,
        stack_cfg.stack_size,
        16,
        true);
    if (!thread) {
        res.error_message = "Failed to create user main thread";
        return res;
    }

    thread->tcb().pc = loaded_img.entry_point;
    thread->tcb().sp_el0 = stack_res.final_sp_el0;
    thread->tcb().spsr_el1 = 0x00ULL;
    thread->tcb().gpr[0] = stack_res.argc;
    thread->tcb().gpr[1] = stack_res.argv_va;
    thread->tcb().gpr[2] = stack_res.envp_va;

    proc->add_thread(thread);
    sched.enqueue_thread(thread);

    // 9. Configure Virtual CPU & Stage-1 MMU
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(0x00300000ULL);

    const std::uint64_t vbar_pa = ppm.allocate_page();
    const std::uint64_t vbar_va = 0x80000ULL;
    cpu.set_vbar_el1(vbar_va);
    mem.write_u32(static_cast<std::size_t>(vbar_pa + 0x400u), 0xD503201Fu); // NOP
    uas.map_user_page(vbar_va, vbar_pa, memory::PagePermissions::UserExecute);

    const std::uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(uas.root_table_pa());
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u));

    if (!sched.dispatch_next(cpu)) {
        res.error_message = "Failed to dispatch user thread to CPU";
        return res;
    }

    cpu.set_current_el(0);
    cpu.set_program_counter(loaded_img.entry_point);
    cpu.set_sp_el0(stack_res.final_sp_el0);

    // 10. Execution Loop
    std::size_t step = 0;
    while (step < max_steps) {
        if (proc->is_terminated()) {
            break;
        }

        const std::uint64_t cur_pc = cpu.get_program_counter();
        const unsigned cur_el = static_cast<unsigned>(cpu.get_current_el());

        const std::uint64_t insn_pa = uas.translate(cur_pc);
        std::uint32_t insn = 0;
        if (insn_pa != 0 && insn_pa + 4 <= mem.size()) {
            insn = mem.read_u32(static_cast<std::size_t>(insn_pa));
        }

        UserspaceTraceStep trace_step{};
        trace_step.step_index = step;
        trace_step.pc = cur_pc;
        trace_step.current_el = cur_el;
        trace_step.instruction = insn;

        cpu.step(mem);
        ++res.instructions_retired;

        if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
            trace_step.is_syscall = true;
            trace_step.syscall_num = (cpu.read_register(16) != 0) ? cpu.read_register(16) : cpu.read_register(0);

            dispatcher.handle_syscall(cpu, *proc, uas, sched);
            ++res.syscalls_handled;
            trace_step.syscall_ret = cpu.read_register(0);

            if (!proc->is_terminated()) {
                cpu.set_current_el(0);
                cpu.set_program_counter(cpu.get_elr_el1());
            }
        }

        res.trace.push_back(trace_step);
        ++step;

        if (cpu.is_waiting_for_interrupt()) {
            break;
        }
    }

    res.success = (res.instructions_retired > 0 && res.syscalls_handled > 0);
    res.exit_code = proc->exit_code();
    res.console_output = console.get_output();
    return res;
}


} // namespace winios::runtime
