# WINIOS CAMPAIGN 191-U — STAGE U3: REAL COMPILED XNU MACH TRAP CLIENT PROOF

**Campaign:** 191-U  
**Stage:** U3 — Real Compiled Mach Trap Client (`mach_client`) & XNU Trap ABI Reconciliation  
**Status:** PASS (3/3 tests passing in `stage_u3_real_mach_trap_tests`; 15/15 legacy/updated Mach IPC CTest targets passing; 1,898/1,898 `cpu_tests` checks passing)

---

## 1. Objective & Provenance

Before Stage U3, WinIOS's Mach IPC layer had only been exercised by hand-assembled or synthetic Mach-O tests built around winios-internal trap numbers (`-26` = `mach_msg_trap`, `-27` = `mach_reply_port`), which diverged from upstream XNU (`osfmk/kern/syscall_sw.c`).

Stage U3:
1. Audits and reconciles WinIOS's Mach trap table and C ABI structs against public Apple XNU / macOS SDK headers (`<mach/mach.h>`, `<mach/message.h>`, `<mach/mach_traps.h>`, `osfmk/kern/syscall_sw.c`).
2. Compiles a real C Mach IPC client (`tests/artifacts/src/mach_client.c` -> `tests/artifacts/bin/mach_client`) with `-fstack-protector-strong`, dynamically linked against `/usr/lib/libSystem.B.dylib` and loaded in **EL0** by guest `minidyld`.
3. Executes full task/thread/reply-port creation, client->service->reply round-trip `mach_msg()` IPC with reply port right translation (`MACH_MSG_TYPE_MAKE_SEND` -> `MACH_MSG_TYPE_MOVE_SEND`), and error propagation (`MACH_SEND_INVALID_DEST`, `MACH_RCV_TIMED_OUT`) on the WinIOS ARM64 CPU.

### Artifact Provenance

| Property | Value |
| :--- | :--- |
| **Source File** | `tests/artifacts/src/mach_client.c` (original test client using `<mach/mach.h>`, `<mach/message.h>`, `<mach/mach_traps.h>`) |
| **Binary File** | `tests/artifacts/bin/mach_client` |
| **Compiler Command** | `zig cc -target aarch64-macos -O2 -mstrict-align -fstack-protector-strong tests/artifacts/src/mach_client.c -o tests/artifacts/bin/mach_client` |
| **Size** | 50,888 bytes |
| **SHA-256** | `e74593b477900d9d44a4089e96baf853259f1db31e363d05aaec2c2e41364a58` |
| **Entry Point** | `0x100001628` (`_main` via `LC_MAIN`) |
| **Dynamic Dependency** | `/usr/lib/libSystem.B.dylib` (`70,960 B`, SHA-256 `eb2050ee18f1dd86ff54c06692764c11582e8afd8a8b7a2796a6c78e49e141ac`) loaded in EL0 by `minidyld` |

---

## 2. XNU ABI & Trap Number Reconciliation

### XNU Cross-Verification (`static_assert` / `_Static_assert`)
Validated at compile time on BOTH host C++ (`tests/unit/stage_u3_real_mach_trap_tests.cpp`) and target ARM64 Darwin C (`tests/artifacts/src/mach_client.c`):

| Symbol / Trap | Public XNU (`osfmk/kern/syscall_sw.c` / `<mach/message.h>`) | WinIOS (`DarwinMachTrap` / `MachMsgHeaderRaw`) | Verified |
| :--- | :--- | :--- | :---: |
| `mach_reply_port` | `-26` | `DarwinMachTrap::MachReplyPort = -26` | **MATCH** |
| `thread_self_trap` | `-27` | `DarwinMachTrap::ThreadSelf = -27` | **MATCH** |
| `task_self_trap` | `-28` | `DarwinMachTrap::TaskSelf = -28` | **MATCH** |
| `host_self_trap` | `-29` | `DarwinMachTrap::HostSelf = -29` | **MATCH** |
| `mach_msg_trap` | `-31` | `DarwinMachTrap::MachMsg = -31` | **MATCH** |
| `mach_msg_overwrite_trap` | `-32` | `DarwinMachTrap::MachMsgOverwrite = -32` | **MATCH** |
| `mach_msg_header_t` | `sizeof == 24` (`0,4,8,12,16,20`) | `sizeof(MachMsgHeaderRaw) == 24` (`0,4,8,12,16,20`) | **MATCH** |
| `MACH_SEND_MSG` / `MACH_RCV_MSG` | `0x1` / `0x2` | `kMachSendMsg = 0x1` / `kMachRcvMsg = 0x2` | **MATCH** |
| `MACH_SEND_INVALID_DEST` / `MACH_RCV_TIMED_OUT` | `0x10000003` / `0x10004003` | `kMachSendInvalidDest` / `kMachRcvTimedOut` | **MATCH** |

### Honest Disclosure: Legacy Trap Number Correction
Earlier WinIOS stages (130–185) mistakenly used `-26` for `MachMsg` and `-27` for `MachReplyPort`. In Stage U3, this mismatch was corrected to authentic XNU numbers across **6 runtime/builder files** and **11 synthetic unit tests**:
- Runtime/Builders: `src/runtime/darwin_syscall_dispatcher.{h,cpp}`, `src/runtime/darwin_runtime_library.cpp`, `src/runtime/darwin_mach_ipc.cpp`, `src/runtime/darwin_userspace_engine.cpp`, `src/{loader,tests/support}/macho_corpus_builder.cpp`.
- Updated Unit Tests: `darwin_sysreg_and_trap_tests.cpp`, `stage130`, `stage134`, `stage143`, `stage145`, `stage154`, `stage159`, `stage169`, `stage173`, `stage179`, `stage181`.
- Multi-port `mach_reply_port` allocation (`src/runtime/darwin_userspace_engine.cpp`): First call allocates legacy/canonical `pid * 100 + 2`; subsequent calls allocate fresh reply ports via `ipc_.mach_reply_port(pid)` (`259`, ...), preserving backward compatibility while supporting concurrent service + reply ports.

---

## 3. Real-Artifact Signals & Fixes Driven by `mach_client`

1. **AArch64 Strict Alignment Flag (`-mno-unaligned-access` -> `-mstrict-align`):** Clang ignores `-mno-unaligned-access` on `aarch64-macos` and emitted unaligned `LDR Q0`/`LDR D0` loads against unaligned `__TEXT.__cstring` strings while MMU is disabled (`SCTLR_EL1.M=0`). Recompiling `libSystem.B.dylib` and `mach_client` with AArch64 `-mstrict-align` resolved the Device-mode alignment faults without relaxing architectural MMU-off CPU checks.
2. **Missing AArch64 Advanced SIMD Instructions (`src/cpu/cpu.cpp`):** Clang `-O2` vectorized the 4-field reply validation in `mach_client.c` using two unimplemented SIMD encodings:
   - `INS (element)` (`0x6E180420` `INS V0.D[1], V1.D[0]`, `0x6E061401` `INS V1.H[1], V0.H[1]`): implemented execution (`bit29=1`, `Q=1`, `imm5`/`imm4` element copy) and disassembly.
   - `SSHLL` / `USHLL` / `SXTL` / `UXTL` (`0x0F10A420` `SXTL V0.4S, V1.4H` / `SSHLL V0.4S, V1.4H, #0`): implemented widening shift-left-long (`opcode == 0b10100`, `esz < 3`) execution and disassembly.
   - Added independent unit tests (`campaign_191u_ins_element_and_sshll_tests`) in `tests/unit/cpu_tests.cpp` (**1,898/1,898 checks passing**).

---

## 4. Verification Results (`stage_u3_real_mach_trap_tests`)

All 3 tests pass (`3 passed, 0 failed`):

| Test | Description | Instructions Retired | Traps / Syscalls | Verified Output / Assertions | Result |
| :--- | :--- | :---: | :---: | :--- | :---: |
| **Test 1** | `mach_client` Provenance & XNU ABI | N/A | N/A | `50,888 B`, SHA-256 verified, links `/usr/lib/libSystem.B.dylib`, compile-time `_Static_assert`s pass | **PASS** |
| **Test 2** | Guest EL0 `mach_client` Execution | 22,285 | 12 Mach traps, 18 BSD syscalls | `MACH_PORTS task=70101,70101 thread=70103 svc=70102 reply=259`, `ROUNDTRIP_OK seq=43 body=PONG_TO_REPLY_PORT`, `ERR_CODES dest=0x10000003 empty=0x10004003`, `MACH_CLIENT_PASS`, exit `0` | **PASS** |
| **Test 3** | Direct XNU Trap Table Verification | 10 | 5 Mach traps | `-26`=`70202`, `-27`=`70203`, `-28`=`70201`, `-29`=`0x101`, `-31` returns `MACH_SEND_INVALID_DEST` (`0x10000003`) without mutating `PSTATE.C` | **PASS** |

---

## 5. Evidence Classification
- **`mach_client`**: `REAL-ARTIFACT-EXECUTED` (dynamically linked against compiled `libSystem.B.dylib` by guest `minidyld`, executing genuine XNU negative-`X16` `SVC #0x80` Mach traps in EL0).
