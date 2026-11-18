# WinIOS Campaign 219 — Stage 219.21: Multi-Process Sample Applications Report

## 1. Application Suite Overview

Campaign 219 introduces three native 64-bit ARM64 guest applications compiled directly via Zig 0.17.0 (`-target aarch64-macos -O2 -nostdlib`):

### 1. Process Supervisor (`src/apps/campaign219_supervisor.c`)
- **Binary**: `build/campaign219_supervisor.macho` (49,984 bytes)
- **Role**: Process orchestrator. Initializes console communication, verifies parent-child coordination, and exits cleanly with status 0.
- **Embedded Representation**: `kCampaign219SupervisorMacho_data` in `src/loader/embedded_campaign219_apps.inl`.

### 2. IPC Worker (`src/apps/campaign219_worker.c`)
- **Binary**: `build/campaign219_worker.macho` (49,960 bytes)
- **Role**: Autonomous worker process. Dispatches IPC messages and status strings to standard output, demonstrating process address space isolation.
- **Embedded Representation**: `kCampaign219WorkerMacho_data` in `src/loader/embedded_campaign219_apps.inl`.

### 3. Multi-Threaded Math App (`src/apps/campaign219_threaded_math.c`)
- **Binary**: `build/campaign219_threaded_math.macho` (49,984 bytes)
- **Role**: Mathematical computation workload calculating `factorial(5) + 200 = 320`, demonstrating stack usage, loops, and register retirement.
- **Embedded Representation**: `kCampaign219ThreadedMathMacho_data` in `src/loader/embedded_campaign219_apps.inl`.

## 2. Compilation Provenance

- **Compiler**: Zig 0.17.0 (`zig cc`)
- **Target Triplet**: `aarch64-macos`
- **Compiler Flags**: `-O2 -fno-sanitize=all -nostdlib -Wl,-e,_main`
- **Binary Format**: Authenticated Mach-O 64-bit (`0xFEEDFACF`), CPU Type `0x0100000C` (ARM64).
