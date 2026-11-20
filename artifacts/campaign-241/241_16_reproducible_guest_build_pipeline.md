# Stage 241.16: Reproducible Guest Build Pipeline

## 1. Build Pipeline Inventory
All 4 guest workloads in `guest-sdk/examples/` built reproducibly:

1. `arith_control` (`main.c` -> `arith_control.macho`)
2. `mem_func` (`main.c` -> `mem_func.macho`)
3. `syscall_lifecycle` (`main.c` -> `syscall_lifecycle.macho`)
4. `multi_app_worker` (`main.c` -> `multi_app_worker.macho`)

Embedded C++ byte arrays in `src/loader/embedded_campaign240_workloads.inl` match the compiled on-disk binaries bit-for-bit.
