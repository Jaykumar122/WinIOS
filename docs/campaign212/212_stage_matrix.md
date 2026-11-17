# WinIOS Campaign 212: Stage Execution Matrix

**Campaign:** 212  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Toolchain:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0  
**Baseline Commit:** `9ee8978b54e743945a0b6b547cd1ff5ae2bdc315`  
**Execution Policy:** Freestanding ARM64 Mach-O execution at EL0 with MMU Stage-1 16KB translation  

| Stage | Name | Description | Key Artifacts / Sources | Verification Status |
|---|---|---|---|---|
| **212.0** | Baseline Audit & Verification Addendum | Verify Campaign 211 closeout commit, inspect ArmTrapFrame packed size (304 bytes), verify repository cleanliness | `docs/campaign212/212_baseline_audit.md` | **PASSED** (100%) |
| **212.1** | Architecture Design & Contract Specification | Design Darwin userspace contracts: Process lifecycle, VFS, Mach IPC, Multi-Process Scheduler | `docs/campaign212/212_dynamic_linking_research.md` | **PASSED** (100%) |
| **212.2** | Darwin Syscall ABI Conformance & Dispatch | Conformance testing across DarwinStrict, WinIOSFast, and AutoDetect ABIs; SPSR carry flag reporting | `src/kernel/user_syscall_dispatcher.cpp`, `stage212_2_syscall_abi_conformance_tests` | **PASSED** (100%) |
| **212.3** | Process Lifecycle & Hierarchy Management | BsdProcess, BsdProcessManager, child lists, orphan reparenting to PID 1, wait4 status formatting | `src/kernel/bsd_process.cpp`, `stage212_3_process_lifecycle_tests` | **PASSED** (100%) |
| **212.4** | Process Spawner & Address Space Isolation | ProcessSpawner spawning child processes with independent UserAddressSpace and stack rollback | `src/kernel/process_spawner.cpp`, `stage212_4_process_spawn_tests` | **PASSED** (100%) |
| **212.5** | Child Process Waiting & Reaping | SYS_wait4 (7) with PID matching, WNOHANG polling, zombie reaping, exit code propagation | `src/kernel/bsd_process.cpp`, `stage212_5_wait_reap_tests` | **PASSED** (100%) |
| **212.6** | Mach-O Dynamic Linking Research | Architectural survey of LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_ID_DYLIB, and dyld bootstrap | `docs/campaign212/212_dynamic_linking_research.md` | **PASSED** (100%) |
| **212.7** | Dynamic Library Dependency Graph Resolver | MachoDependencyGraph with DFS cycle detection, DAG topological resolution, missing dylib detection | `src/loader/macho_dependency_graph.cpp`, `stage212_7_dependency_graph_tests` | **PASSED** (100%) |
| **212.8** | Guest Dynamic Loader | GuestDynamicLoader mapping non-overlapping library VAs, enforcing W^X, segment permissions | `src/loader/guest_dynamic_loader.cpp`, `stage212_8_dynamic_loader_tests` | **PASSED** (100%) |
| **212.9** | Relocation Binding & GOT Resolution | Non-lazy symbol binding, GOT pointer resolution, rebasing against load addresses | `src/loader/guest_dynamic_loader.cpp`, `stage212_9_relocation_tests` | **PASSED** (100%) |
| **212.10** | Minimal Dynamic Runtime Library | Freestanding ARM64 libwinios_runtime.dylib compiled with Zig 0.17.0, exported dyld stub symbols | `artifacts/campaign-212/libwinios_runtime.dylib`, `stage212_10_runtime_library_tests` | **PASSED** (100%) |
| **212.11** | Dynamic Application Startup | UserspaceCoordinator launching genuine ARM64 Mach-O with dylib loading, EL0 retirement, 5 syscalls | `src/runtime/userspace_coordinator.cpp`, `stage212_11_dynamic_startup_tests` | **PASSED** (100%) |
| **212.12** | VFS Architecture Audit & Extension | VirtualFileSystem with standard Darwin hierarchy (/dev, /tmp, /usr), character devices, stat64 | `src/vfs/vfs.cpp`, `stage212_12_vfs_tests` | **PASSED** (100%) |
| **212.13** | File Operations & Syscall Expansion | SYS_open (5), SYS_close (6), SYS_read (3), SYS_write (4), SYS_lseek (199), SYS_stat64 (339) | `src/kernel/user_syscall_dispatcher.cpp`, `stage212_13_file_ops_tests` | **PASSED** (100%) |
| **212.14** | Pipes & Inter-Process Streams | Bounded 64KB circular ring buffer Pipe, SYS_pipe (42), EPIPE handling, EOF on closed writer | `src/kernel/pipe.cpp`, `stage212_14_pipe_tests` | **PASSED** (100%) |
| **212.15** | Bootstrap Service Registry Architecture | BootstrapServiceRegistry with check_in, look_up, unregister, dead-name handling, PID 1 privilege | `src/kernel/bootstrap_registry.cpp`, `stage212_15_bootstrap_registry_tests` | **PASSED** (100%) |
| **212.16** | Mach IPC Reliability & Port Space Audit | ProcessIpcSpace port rights (Receive, Send, SendOnce), bounded 64-byte message queues | `src/kernel/ipc.cpp`, `stage212_16_mach_ipc_tests` | **PASSED** (100%) |
| **212.17** | Init Coordinator & Child Supervision | InitCoordinator (PID 1 launchd), supervised service lifecycle, crash detection, zombie reaping | `src/kernel/init_coordinator.cpp`, `stage212_17_init_coordinator_tests` | **PASSED** (100%) |
| **212.18** | Multi-Process Context Switching & Scheduling | MultiProcessScheduler round-robin scheduling, 4-tick quantum preemption, TTBR0 switching | `src/kernel/multi_process_scheduler.cpp`, `stage212_18_multi_process_scheduler_tests` | **PASSED** (100%) |
| **212.19** | Workload Construction: Three Freestanding ARM64 Mach-O Binaries | Genuine ARM64 assembly binaries: init_coordinator.s, service_daemon.s, client_app.s | `artifacts/campaign-212/*.macho`, `stage212_19_workload_assembly_tests` | **PASSED** (100%) |
| **212.20** | Multi-Process End-to-End Execution Demonstration | Concurrent EL0 execution: 80 retired instructions, 16 syscalls, 7 TTBR0 switches, supervisor reaping | `tests/unit/stage212_20_multi_process_e2e_tests.cpp`, `stage212_20_multi_process_e2e_tests` | **PASSED** (100%) |
| **212.21** | Snapshot & Deterministic Replay | Combined serialization & streaming deserialization of proc_mgr, bootstrap, scheduler, dispatcher | `src/kernel/multi_process_scheduler.cpp`, `stage212_21_snapshot_replay_tests` | **PASSED** (100%) |
| **212.22** | 25-Vector Adversarial Security Suite | 25 security vectors covering Mach-O fuzzing, W^X violations, EFAULT null pointers, EPIPE, ELOOP | `tests/unit/stage212_22_adversarial_security_tests.cpp`, `stage212_22_adversarial_security_tests` | **PASSED** (100%) |
| **212.23** | Documentation, Benchmarks, Verification Artifacts | Comprehensive architectural matrix, verification evidence, benchmarks, SHA-256 manifest | `docs/campaign212/` | **PASSED** (100%) |
| **212.24** | Full Regression Suite, SHA-256 Manifest & Closeout | Full repository 670-test regression verification, cryptographic integrity manifest, Git commit | `artifacts/campaign-212/212_sha256_manifest.txt`, Git commit | **PASSED** (100%) |
