#include "kernel/process_spawner.h"

#include <algorithm>

namespace winios::kernel {

SpawnResult ProcessSpawner::spawn(
    const std::vector<std::uint8_t>& binary,
    const SpawnConfig& config,
    BsdProcessManager& proc_mgr,
    KernelThreadManager& thread_mgr,
    memory::PhysicalPageManager& ppm,
    Memory& mem) {
    SpawnResult res{};

    // 1. Validate basic input
    if (binary.empty()) {
        res.error_message = "Empty Mach-O binary";
        return res;
    }

    // 2. Allocate root L2 page table
    const std::uint64_t root_l2_pa = ppm.allocate_page();
    if (root_l2_pa == 0) {
        res.error_message = "Failed to allocate root L2 page table from physical page manager";
        return res;
    }

    // 3. Create independent UserAddressSpace
    auto uas = std::make_unique<memory::UserAddressSpace>(
        mem, ppm, root_l2_pa, config.process_name + "_vm");

    // 4. Load Mach-O executable image into address space
    auto loaded_img = loader::MachoUserLoader::load_image(binary, *uas, ppm, mem);
    if (!loaded_img.valid) {
        res.error_message = "Mach-O load failed: " + loaded_img.error_message;
        // Rollback: uas destructor frees root_l2_pa and any allocated pages
        return res;
    }

    // 5. Construct user stack
    UserStackConfig stack_cfg;
    stack_cfg.argv = config.argv;
    stack_cfg.envp = config.envp;
    stack_cfg.stack_size = std::min(loaded_img.stack_size, config.max_stack_size);
    if (stack_cfg.stack_size == 0) {
        stack_cfg.stack_size = 0x10000; // Default 64 KiB
    }

    auto stack_res = UserStackBuilder::build_stack(*uas, ppm, mem, stack_cfg);
    if (!stack_res.success) {
        res.error_message = "User stack construction failed";
        return res;
    }

    // 6. Allocate BSD Process in manager
    auto* proc = proc_mgr.create_process(config.process_name, config.parent_pid);
    if (!proc) {
        res.error_message = "Failed to allocate BSD process entry";
        return res;
    }
    const pid_t pid = proc->pid();

    // 7. Create user main thread
    auto* thread = thread_mgr.create_thread(
        config.process_name + "_main",
        loaded_img.entry_point,
        pid,
        stack_res.final_sp_el0,
        stack_cfg.stack_size,
        16, // Default priority
        true);

    if (!thread) {
        // Rollback created process
        proc_mgr.terminate_process(pid, -1);
        proc_mgr.reap_process(pid);
        res.error_message = "Failed to allocate user main thread";
        return res;
    }

    // 8. Configure architectural register state
    thread->tcb().pc = loaded_img.entry_point;
    thread->tcb().sp_el0 = stack_res.final_sp_el0;
    thread->tcb().spsr_el1 = 0x00ULL; // Mode EL0t, DAIF unmasked
    thread->tcb().gpr[0] = stack_res.argc;
    thread->tcb().gpr[1] = stack_res.argv_va;
    thread->tcb().gpr[2] = stack_res.envp_va;

    proc->add_thread(thread);

    res.success = true;
    res.pid = pid;
    res.process = proc;
    res.main_thread = thread;
    res.address_space = std::move(uas);
    return res;
}

bool ProcessSpawner::destroy_process(
    pid_t pid,
    SpawnResult& spawn_res,
    BsdProcessManager& proc_mgr,
    KernelThreadManager& thread_mgr) {
    if (spawn_res.main_thread) {
        thread_mgr.terminate_thread(spawn_res.main_thread->tid(), 0);
        thread_mgr.reap_terminated_threads();
        spawn_res.main_thread = nullptr;
    }

    if (spawn_res.process) {
        proc_mgr.terminate_process(pid, 0);
        proc_mgr.reap_process(pid);
        spawn_res.process = nullptr;
    }

    spawn_res.address_space.reset();
    spawn_res.success = false;
    spawn_res.pid = 0;
    return true;
}

} // namespace winios::kernel
