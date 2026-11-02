# Inferno process launcher (experimental)

WinIOS can start a **separately built** ChefKiss Inferno emulator as a child
process. This is a process-integration milestone: WinIOS does not embed, link,
vendor or redistribute QEMU/Inferno, and nothing in this document means that iOS
boots or that iOS applications work.

## What it does

- Starts the configured `qemu-system-aarch64.exe` with an explicit argument list.
- Adds the configured MSYS2 UCRT64 `bin` directory to the **child's** `PATH` only.
- Captures stdout and stderr with a per-stream size bound.
- Reports the exact child exit code, or a typed launch failure.
- Terminates the child when a timeout expires.
- Can run the emulator's own `--version`, `-M help`, and `-M t8030,help` queries.

It never runs a guest machine by itself: the default `win_ios.exe` demo is
unchanged, and a guest is only started if someone invokes the emulator directly.

## Configuration

| Setting | Meaning |
|---|---|
| Executable path | Full path to the `qemu-system-aarch64.exe` built from the pinned Inferno revision `cc4302a99167abec69b714cfd00c38caece7e7de`. |
| UCRT64 runtime directory | The MSYS2 `ucrt64\bin` directory containing the DLLs the emulator loads (on this machine `D:\WinISO26\Inferno-Research\msys2root\ucrt64\bin`). |
| Working directory | Optional. Defaults to the directory containing the executable. |
| Timeout | Default 30 s. The child is terminated when it expires. |
| Output limit | Default 256 KiB per stream. Further output is drained but discarded and flagged as truncated. |

The executable and runtime directory are never guessed and never substituted
with another QEMU build. If the configured executable is missing the launcher
fails with `executable_missing` rather than falling back to a system QEMU.

## Command line

```powershell
# Show the effective configuration and whether it validates.
win_ios.exe --inferno info --inferno-exe <path> --inferno-runtime <dir>

# Query the emulator. The identity of the build is checked, not assumed.
win_ios.exe --inferno version  --inferno-exe <path> --inferno-runtime <dir>
win_ios.exe --inferno machines --inferno-exe <path> --inferno-runtime <dir>
win_ios.exe --inferno t8030    --inferno-exe <path> --inferno-runtime <dir>

# Optional: change the timeout or working directory.
win_ios.exe --inferno version --inferno-exe <path> --inferno-runtime <dir> `
            --inferno-timeout 60000 --inferno-workdir <dir>
```

`--inferno-exe` and `--inferno-runtime` may be omitted when the read-only
environment variables `WINIOS_INFERNO_EXE` and `WINIOS_INFERNO_RUNTIME` are set
for the current process:

```powershell
$env:WINIOS_INFERNO_EXE="D:\WinISO26\Inferno-Research\Inferno\build\qemu-system-aarch64.exe"
$env:WINIOS_INFERNO_RUNTIME="D:\WinISO26\Inferno-Research\msys2root\ucrt64\bin"
win_ios.exe --inferno version
```

The process environment is only read. WinIOS never writes environment
variables, never touches the machine-wide environment, and needs no
administrator rights.

Exit codes: `0` success and the expected content was found, `1` launch failure
or unexpected content, `2` command-line usage error.

## Failure kinds

| Kind | Meaning |
|---|---|
| `invalid_configuration` | Empty path, a directory where a file is required, a non-positive timeout, or a zero output limit. |
| `executable_missing` | The configured emulator executable does not exist. |
| `runtime_directory_missing` | The configured UCRT64 runtime directory does not exist or is not a directory. |
| `process_creation_failed` | `CreateProcessW`, pipe creation, or waiting for the child failed. |
| `dll_initialization_failed` | The child exited with `0xC0000135`, `0xC0000139` or `0xC000007B`, i.e. a DLL was missing or unusable. The message names the runtime directory to check. |
| `timeout` | The child did not exit in time and was terminated. |
| `nonzero_exit` | The child ran and returned a non-zero exit code. |
| `unsupported_platform` | The launcher is Windows-only. |

## Implementation notes

- Native Win32 only: `CreatePipe`, a restricted inherited-handle list
  (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), `CreateProcessW`, `PeekNamedPipe`,
  `WaitForSingleObject`, `TerminateProcess`. No `cmd.exe`, no PowerShell, no
  shell string concatenation.
- Arguments are quoted with the Microsoft C runtime rules, so spaces, embedded
  quotes and trailing backslashes survive in the child's `argv`.
- The child receives a copy of the parent's environment block with exactly one
  change: the runtime directory is prepended to `PATH` (and is not added twice).
  The parent block is not modified.
- Both streams are drained with `PeekNamedPipe` before each read on every
  iteration, and the child is waited on in short slices, so a chatty or
  silent stream on either side can neither deadlock WinIOS nor starve the
  other stream. Output beyond the configured bound is read and discarded, so
  the child cannot block on a full pipe.

## Tests

```powershell
# Part of the normal suite; uses a local test fixture, never Inferno.
ctest --test-dir WinIOS\build -C Debug -R inferno_launcher_tests --output-on-failure

# Opt-in integration checks against the local Inferno build.
$env:WINIOS_INFERNO_EXE="D:\WinISO26\Inferno-Research\Inferno\build\qemu-system-aarch64.exe"
$env:WINIOS_INFERNO_RUNTIME="D:\WinISO26\Inferno-Research\msys2root\ucrt64\bin"
ctest --test-dir WinIOS\build -C Debug -R inferno_integration --output-on-failure
```

`inferno_integration_tests` returns CTest's skip code `77` when the two
environment variables are not set or point at something that does not exist, so
the default `ctest` run never depends on an Inferno checkout. A skip is reported
as `***Skipped`, not as a pass.

## Limitations

- **TCG only.** The emulator is built without hardware acceleration: KVM and HVF
  are not applicable on Windows, and the pinned revision contains **no Windows
  Hypervisor Platform (WHPX) backend**, so `-accel whpx` is unavailable. WinIOS
  does not add one and makes no acceleration claim. Guest execution is therefore
  interpreted/JIT-translated and slow.
- **Windows is not a supported host of the emulator project.** Its CI covers
  Linux x86_64 and macOS only; this integration is validated only against one
  local Windows build.
- **Runtime DLLs are required.** The emulator is dynamically linked against the
  MSYS2 UCRT64 runtime; without the configured directory on the child's `PATH` it
  fails at load time (reported as `dll_initialization_failed`). Nothing is
  copied into the WinIOS tree.
- **No firmware, no boot.** WinIOS never supplies an IPSW, iOS kernel,
  SecureROM, SEP firmware/ROM, AP ticket or TrustCache, and the integration tests
  only run `--version` and `-M ...` queries.
- **Machine registration is not compatibility.** `t8030` appearing in `-M help`
  and in `-M t8030,help` proves that the machine model is compiled and
  registered. It does not demonstrate a successful iOS boot, driver behaviour,
  SEP functionality, or that any iOS application runs. No such claim is made.
