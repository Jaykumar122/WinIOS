# WINIOS CAMPAIGN 191-U — STAGE U2: COMPILED `libSystem-Lite` & STANDARD C EXECUTION PROOF

**Campaign:** 191-U  
**Stage:** U2 — Real Compiled `libSystem.B.dylib` (`libSystem-Lite`) + Standard C Executables  
**Status:** PASS (4/4 unit & artifact execution tests passing in `stage_u2_libsystem_lite_tests`)

---

## 1. Objective & Provenance

Before Stage U2, compiled Mach-O guest programs were `-nostdlib` freestanding binaries writing their own inline `SVC #0x80` syscall wrappers, and `minidyld` bypassed missing `/usr/lib/libSystem.B.dylib` while providing host-fallback stubs for `___stack_chk_guard` and `___stack_chk_fail`.

Stage U2 replaces those host-synthesized canary stubs with an original compiled C runtime shared library (`tests/artifacts/src/libsystem_lite.c` -> `/usr/lib/libSystem.B.dylib`) dynamically loaded and bound in **EL0** by `minidyld`, executing **non-freestanding** C applications compiled against standard `<stdio.h>`, `<stdlib.h>`, `<string.h>`, and `<unistd.h>` headers with `-fstack-protector-strong`. (BSD syscalls, Mach IPC traps, VFS/process state, and mapping of the initial main + `minidyld` images remain host-emulated.)

### Artifact Provenance

| Artifact | Size | SHA-256 | Compile Flags / Role |
| :--- | :---: | :--- | :--- |
| `tests/artifacts/bin/libSystem.B.dylib` | 70,960 B | `eb2050ee18f1dd86ff54c06692764c11582e8afd8a8b7a2796a6c78e49e141ac` | `zig cc -target aarch64-macos -dynamiclib -O2 -nostdlib -fno-builtin -fno-stack-protector -mstrict-align -install_name /usr/lib/libSystem.B.dylib -Wl,-undefined,dynamic_lookup` (44 exports, `mach_task_self_` constructor) |
| `tests/artifacts/bin/stdio_heap_app` | 51,024 B | `2442509602e4faa265e1f9f43ee55e5698dd862a7cc213bf9cb4ce8da6964f65` | `zig cc -target aarch64-macos -O2 -fstack-protector-strong` against standard `<stdio.h>/<stdlib.h>/<string.h>/<unistd.h>` |
| `tests/artifacts/bin/stack_smash_libc_app` | 50,392 B | `be8983f853fe8f36ce96e91f44a88c023da6e31ebaa41a300f95d1a8dfa54d54` | `zig cc -target aarch64-macos -O2 -fstack-protector-strong` against standard `<stdio.h>` |

NOTE: `libsystem_lite.c` is original code with zero Apple/APSL source code.

---

## 2. Exported Interfaces Implemented in `libSystem.B.dylib`

1. **Raw BSD Syscalls (`SVC #0x80`):** `_exit`, `exit`, `read`, `write`, `open`, `close`, `getpid`, `mmap`, `munmap`, `abort`.
2. **Raw Mach Traps (`SVC #0x80`, negative `X16`):** `mach_reply_port` (`-26`), `mach_task_self` (`-28`), `mach_msg_trap` (`-31`).
3. **16-Byte Aligned `mmap`-Backed Heap Allocator:** `malloc`, `free`, `calloc`, `realloc` (backed by anonymous `SYS_mmap` 64 KB chunks in guest virtual memory `0x01000000+` and `SYS_munmap` for freed large regions).
4. **String & Memory Primitives (+ `_FORTIFY_SOURCE` Checked Variants):** `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `memcpy`, `memmove`, `memset`, `memcmp`, `__strcpy_chk`, `__memcpy_chk`, `__memset_chk`, `__memmove_chk`.
5. **Formatted Stdio:** `putchar`, `puts`, `vsnprintf`, `snprintf`, `printf`.
6. **Compiler Stack Protector Runtime:** `__stack_chk_guard`, `__stack_chk_fail` (writes `"*** stack smashing detected ***: terminated\n"` to `fd=2` stderr and exits with code `134`).

---

## 3. Runtime & Guest Linker Extensions Driven by Stage U2

1. **Separated `stdout_output` / `stderr_output` (`src/runtime/darwin_userspace_engine.h/.cpp`):** Updated `SYS_write` (`nr == 4`) so `fd == 1` appends to `DarwinPipelineResult::stdout_output` and `fd == 2` appends to `DarwinPipelineResult::stderr_output`, enabling independent assertion on `__stack_chk_fail` diagnostics.
2. **In-RAM `brk_curr` Initialization for `SYS_mmap` (`execute_macho_with_guest_dyld`):** Lowered `proc->brk_curr` to `0x01000000ULL` when running within flat 64 MB guest RAM so `malloc`'s anonymous `mmap` allocations reside inside physical guest memory (`0x01000000..0x01F00000`) without colliding with `minidyld` (`0x02000000`) or the Darwin stack (`0x03F00000`).
3. **Anonymous `SYS_mmap` Zero-Initialization & `SYS_munmap` (`nr == 197`, `nr == 73`):** Anonymous mappings (`MAP_ANON` / `fd < 0`) zero-fill their allocated pages; `SYS_munmap` forwards to `DarwinBsdSubsystem::munmap`.
4. **Real `argc`/`argv`/`envp` Stack Handoff & Rebase Support (`tests/artifacts/src/minidyld.c`):** Added `apply_rebases` for `LC_DYLD_INFO_ONLY` rebase opcodes and forwarded `(argc, argv, envp)` from `initial_sp` (`X1`) to `LC_MAIN`.

---

## 4. Verification Results (`stage_u2_libsystem_lite_tests`)

All 4 tests pass (`4 passed, 0 failed`):

| Test | Scenario / Target | Guest Instructions | BSD Syscalls | Key Verified Outcome | Result |
| :--- | :--- | :---: | :---: | :--- | :---: |
| **Test 1** | Artifact Provenance & Structure | N/A | N/A | Valid ARM64 Mach-O (`libSystem.B.dylib`, `stdio_heap_app`, `stack_smash_libc_app`), `LC_LOAD_DYLIB /usr/lib/libSystem.B.dylib` present | **PASS** |
| **Test 2** | `stdio_heap_app` Standard C Execution | 28,101 | 24 | `PID=601 ARGC=2 PROG=/usr/bin/stdio_heap_app`, `HEAP=WinIOS_Heap_OK LEN=14 ZERO=0 TOTAL=0x64 SNP=0,0`, `FMT NEG=-42 HEX=0000CAFE PCT=% PTR=0x10000f0`, exit `0`; `vm_regions` populated | **PASS** |
| **Test 3** | `stack_smash_libc_app` Protector Abort | 8,031 | >5 | `stdout="BEFORE_SMASH\n"`, `stderr="*** stack smashing detected ***: terminated\n"`, exit `134` | **PASS** |
| **Test 4** | GOT Resolution Provenance | 8,031 | >5 | `GOT[___stack_chk_guard]` (`0x4008`) bound to `0x2c010` inside loaded `libSystem.B.dylib` (`0x20000..0x35000`), not `minidyld` fallback (`0x2000000+`), containing canary `0x595E9D4B1A2C3E4FULL` | **PASS** |

---

## 5. Evidence Classification
- **`libSystem.B.dylib`, `stdio_heap_app`, `stack_smash_libc_app`**: `REAL-ARTIFACT-EXECUTED` (dynamically loaded, symbol-bound via export trie, and executed in EL0 alongside `minidyld`).
