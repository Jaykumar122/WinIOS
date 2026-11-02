# ChefKiss Inferno — Feasibility Research for WinIOS

| Field | Value |
|---|---|
| Report date | 2026-10-29 |
| Subject | ChefKiss Inferno (`ChefKissInc/Inferno`) |
| WinIOS baseline inspected | `830e73e Add AArch64 arithmetic and conditional branch instructions` |
| WinIOS worktree state during inspection | **dirty** — `src/cpu/cpu.cpp` and `tests/unit/cpu_tests.cpp` modified |
| Host used for inspection | Windows 11 x64, AMD Ryzen 5 4600H, 32 GB RAM, GTX 1650 4 GB |
| Inferno code executed on this host | **No** (not cloned, not built, no IPSW involved) |
| Genuine iOS boot observed by this investigation | **No** |

> **Scope note.** Nothing in this report is a claim that WinIOS boots iOS, runs iOS
> applications, or handles IPSWs. Nothing in this report is a claim that Inferno was
> run on this laptop. Section 12 lists every statement that remains unverified.

---

## 0. Bottom line

1. **ChefKiss Inferno is a real, actively developed project**, verified as
   `https://github.com/ChefKissInc/Inferno`. It is a QEMU derivative (a fork of the archived
   `TrungNguyen1909/qemu-t8030`), not a from-scratch emulator and not a standalone binary.
2. **It demonstrably boots one device**: an emulated iPhone 11 (T8030, `n104ap`) running
   iOS 14.x, to SpringBoard, with display output. Its own documentation and user bug reports
   show the iOS kernel actually executing inside it. The iPhone 6s Plus (A9) port is
   explicitly work-in-progress.
3. **It is GPLv3, with the project's own new code under AGPLv3.** Any serious reuse forces
   WinIOS to become a GPLv3-compatible, source-available distribution — including the AGPL
   network clause. This is a product decision, not a technicality.
4. **Windows is a community-supported host, not an official one.** There is no Windows CI,
   no Windows binary, no MSVC build, and no releases at all. But the host guide has Windows
   (PowerShell/CMD) tabs, TCP transport was added specifically for Windows, and a Windows 11
   user with an AMD Ryzen 9 7940H did compile and launch it in 2026 (issue #260).
5. **KVM and WHPX are not usable for the iPhone guest on this laptop.** KVM is an open
   feature request (milestone "Odysseus"), requires an AArch64 host, and WHPX is not present
   in the `accel/` tree at all. Even if it were, an AMD x86_64 CPU cannot hardware-virtualise
   an AArch64 guest. The iPhone will always run under TCG (software translation) here.
6. **Recommendation:** do not continue WinIOS as a path to iOS, and do not vendor Inferno
   into WinIOS. Use Inferno as a separate, externally built process and turn WinIOS into (a) a
   Windows launcher/monitor for it and (b) an AArch64 semantics test bench. The first
   milestone should be a read-only build-and-inspect of Inferno under MSYS2, with no iOS
   firmware involved at all.

---

## 1. Verified project identity and official repository

| Property | Observed value | How verified |
|---|---|---|
| Official repository | `https://github.com/ChefKissInc/Inferno` | GitHub REST API `/repos/ChefKissInc/Inferno` |
| Owner | `ChefKissInc` (organisation) | same |
| Description | "Apple Silicon device emulator." | same |
| Default branch | `master` | same |
| Created | 2023-04-14 | same |
| Last push | 2026-10-28 | same |
| Stars / forks | 4,035 / 330 | same |
| Open issues | 37 | same |
| Repository size | 413,621 KB (~404 MiB) | same |
| Releases | **none published** (`[]`) | `/releases` |
| Upstream parent | `TrungNguyen1909/qemu-t8030`, **archived** | repo API `parent` field |
| Licence metadata | `NOASSERTION` (GitHub cannot classify it) | repo API `license.spdx_id` |
| Official documentation | `https://chefkiss.dev/applehax/inferno` and `https://chefkiss.dev/guides/inferno/` | README link |

Disambiguation: search results also return unrelated repositories such as
`MakrSas/Inferno-iOS` ("an emulated iPhone 11 running iOS 14, as an app on a real iPhone",
explicitly unofficial). These are **not** the project in question. The `ChefKissInc/Inferno`
repository is the one linked from the official ChefKiss site and is the only one whose README
calls itself "ChefKiss Inferno".

Lineage, as stated by both the project and third parties:

- `ChefKiss Inferno` ← fork of `TrungNguyen1909/qemu-t8030` ← derived from QEMU.
- The project README states plainly: *"ChefKiss Inferno is derivative of the QEMU project which
  provides Apple ARM device guest support."*
- The Apple Wiki (community) describes it as having "branched off as a separate project, with
  the code originally added by QEMU-t8030 now mostly rewritten". That "mostly rewritten" claim
  is a community characterisation, not something this investigation measured. The tree
  structure (see §6) is unmistakably QEMU's.

Practical consequence: "integrating Inferno" means "building and shipping a QEMU fork", not
"adding a library".

---

## 2. Licence and dependency findings

### 2.1 Licence text

`LICENSE` (2,439 bytes) is a custom notice layered on top of QEMU's original notice. Its
operative points, quoted:

1. The derivative project is **GPLv3**, having used GPLv2 §9's "or later" upgrade path, and
   "our own parts" are **AGPLv3**, permitted by GPLv3 §13.
2. "Some parts may have different licenses, specific licensing information may be observed in
   each source file. The parts that are licensed only under the GNU General Public License
   version 2, without the 'or later' clause, will be removed in subsequent commits."
3. Branding restrictions apply (GPLv3 §7 additional terms), in `ui/icons/CKBrandingNotice.md`.

The repository also ships `COPYING` (GPLv2, 17,880 bytes), `COPYING.LIB` (LGPL-2.1,
26,415 bytes), `COPYING_GPL-3.0` (35,149 bytes) and `COPYING_AGPL-3.0` (34,523 bytes).

Per-file headers confirm the split. For example, `target/arm/emulate/aarch64.c` — a file
written specifically for this project in 2025–2026 — carries an AGPLv3 header, while
`target/arm/vfp_fpscr.c` carries the inherited LGPL-2.1-or-later header from QEMU.

### 2.2 Consequences for WinIOS

| Question | Answer |
|---|---|
| Can WinIOS copy individual device files into its own tree? | Only if WinIOS is distributed under GPLv3 (and AGPLv3 for the AGPL parts). WinIOS currently has **no licence file at all**. |
| Can WinIOS link Inferno as a library? | No. QEMU is an application, not a library; and GPLv3 would apply to the combined work. |
| Can WinIOS launch Inferno as a separate process and talk to it over a socket? | Yes. This is the only clean separation, and it is the architecture recommended in §9. |
| Does the AGPL reach WinIOS if it merely launches Inferno? | Not through the process boundary by itself, but an AGPL-covered *modified* Inferno exposed over a network must offer its source. Do not modify and redistribute Inferno without legal review. |
| Are there non-code restrictions? | Yes: the branding notice restricts use of ChefKiss/Inferno marks. A WinIOS-branded redistribution that bundles Inferno would need to honour it. |

Neither the README nor `LICENSE` grants any patent licence beyond what GPLv3/AGPLv3 imply, and
neither grants rights to Apple firmware, IPSWs, SEP ROMs, SHSH tickets or trust caches.

### 2.3 Dependencies

Build-time (from the CI workflow and the host-setup guide): `meson`, `ninja`, `pkg-config`,
`glib2`, `pixman`, `nettle` (≥3.10, built from source in CI), `gnutls`, `libslirp`,
`libusb-1.0`, `libssh`, `lzo`, `snappy`, `zstd`, `lzfse`/`liblzfse`, `ncurses`, `dtc`,
`libjpeg-turbo`, `libpng`, `vde2`. The Linux CI installs roughly two dozen packages and
builds nettle by hand, which is a fair indicator of how heavy the toolchain is.

Submodules (`.gitmodules`): the standard QEMU `roms/*` set (seabios, ipxe, openbios,
qemu-palcode, u-boot, skiboot, QemuMacDrivers, u-boot-sam460ex, edk2, opensbi, qboot,
vbootrom) plus `util/mlib` (`P-p-H-d/mlib`). Submodules must be fetched (`submodules: true` in
CI) or the build fails.

Runtime/operational dependencies that are **not** in the repository: `usbmuxd`,
`libimobiledevice`, `libirecovery`, `idevicerestore` (with a project-supplied patch), `img4lib`,
`pyasn1`, `pyasn1-modules`, plus a legally obtained IPSW, a SEP ROM dump and a generated
root ticket.

None of this is compatible with WinIOS's current "CMake + MSVC + five small test executables"
model. Adding it would replace the build system, not extend it.

---

## 3. Host OS and CPU requirements (including KVM, WHPX and floating point)

### 3.1 Documented requirements

From `https://chefkiss.dev/guides/inferno/` (Setup → Prerequisites):

- "A minimum of **6GB of RAM** is required for smooth operation."
- "A minimum of **~32GB free** [disk] is required."
- "Sufficiently fast CPU — Emulation is quite intensive, especially with software rendering."
- "Time and patience … Setting up Inferno is a relatively lengthy process."
- A **companion x86_64 VM** is mandatory for USB/restore/network.

A real-world restore command line (issue #282) uses `-smp 6 -m 4G`, a 32 GB root disk, and
seven NVMe namespaces with 4096-byte logical blocks. That is a reasonable lower bound for
planning: **4 GB guest RAM, 6 vCPUs, ~32 GB disk**.

### 3.2 What this laptop provides

| Requirement | Ryzen 5 4600H / 32 GB / Win11 x64 | Verdict |
|---|---|---|
| ≥ 6 GB host RAM | 32 GB | Pass, comfortably |
| ≥ 32 GB free disk | Not measured (SSD assumed) | Likely pass; must be confirmed |
| x86_64 host with TCG | Yes | Pass |
| AArch64 hardware virtualisation | No (AMD-V virtualises x86 only) | **Cannot pass** |
| Windows host support | Community-supported only | Conditional |

The laptop is adequate for a TCG build. It will not be fast. The GTX 1650 contributes
nothing: Inferno renders with software rendering (see §8).

### 3.3 KVM

- What it is: the Linux kernel's same-architecture hardware virtualisation interface. The guest
  CPU must be the host CPU architecture, because the guest executes natively.
- Status in Inferno: **not implemented**. `accel/Kconfig` exposes TCG, HVF, KVM and Xen;
  issue **#87 "KVM support"** is open, labelled `Area/Host`, assigned, milestone **"Odysseus"**
  (created 2026-01-06, 22 open issues, 0 closed), with Effort = High.
- The issue body shows the current failure mode on an Apple M1 host with 16K pages:
  `kvm_set_user_memory_region: KVM_SET_USER_MEMORY_REGION failed … Invalid argument`, and notes
  "unlike HVF, that at least somewhat boots".
- Maintainer comment on the feasibility of KVM on ARM boards: the Raspberry Pi 5 "doesn't
  support PAC, so you would need to settle for S8000 … it doesn't support EL3 virtualization".
- Relevance to this laptop: **zero, twice over**. There is no KVM on Windows, and even on Linux
  an x86_64 CPU cannot KVM an AArch64 guest.

### 3.4 WHPX

- What it is: Windows Hypervisor Platform, the Windows API that QEMU's `accel/whpx` backend uses
  for same-architecture hardware acceleration (typically x86-on-x86, or ARM64-on-ARM64 on
  Windows on ARM).
- Status in Inferno: **absent**. The `accel/` directory contains exactly `hvf`, `kvm`, `tcg`,
  `xen`, plus the common files — there is no `accel/whpx`. Upstream QEMU has one; this fork
  does not.
- Windows CI was requested and **closed as not planned** (issue **#229 "Windows Github CI"**).
- Relevance: WHPX could at best accelerate the *companion x86_64 VM* (which is a normal QEMU
  x86 target) — something the project has never offered. It cannot accelerate the iPhone.

### 3.5 Floating point

The investigation looked specifically for floating-point work and found only inherited and
incremental material, not a distinct "FP project":

| Path | What it is | Licence |
|---|---|---|
| `fpu/softfloat.c`, `fpu/softfloat-parts.c.inc`, `fpu/softfloat-parts-addsub.c.inc`, `fpu/softfloat-specialize.c.inc`, `fpu/meson.build` | QEMU's soft-float implementation (IEEE-754 in integer arithmetic) | per-file, mostly BSD-3 |
| `target/arm/vfp_fpscr.c` | FPCR/FPSR/FPSCR get/set, including FP16 (`FZ16`), `AFP` (`FIZ`/`AH`/`NEP`), `EBF`/EBF16 gating, and host float-status folding | LGPL-2.1-or-later |
| `target/arm/emulate/aarch64.c` | Project-authored **fallback instruction emulator**; currently only LDP/STP/LDR/STR forms, used when a hypervisor accelerator cannot handle an access | **AGPLv3** |

A GitHub issue/PR search for `float`, `fpcr`, `fp16` in the repository returned **no results**
(2026-10-29). There is no evidence of a separate FP16/BF16/matrix-extension effort, and no
floating-point optimisation that would change performance on an x86_64 host. All guest FP is
emulated.

Two points matter for WinIOS:

1. WinIOS has **no FP/SIMD support at all** (no `FPCR`, `FPSR`, V registers, `FMOV`, `FADD`, NEON).
   iOS userspace is full of FP and NEON. This is an unbounded gap, not a missing instruction or
   two.
2. `target/arm/emulate/aarch64.c` is the single most tempting file in the repository to copy
   into WinIOS, precisely because it looks like WinIOS's own CPU code: a `switch` on an
   instruction word with `get_reg`/`set_reg` callbacks. It is AGPLv3, it has
   `// TODO: Protection checks? lol`, and it only implements four instruction families. It is
   **not** a shortcut and must not be copied before the licence decision in §2.

---

## 4. Verified iOS boot evidence and tested versions

### 4.1 What the project claims

From `https://chefkiss.dev/applehax/inferno` (page returned HTTP 403 to direct fetch; content
obtained from the search index and cross-checked against the repository and issue tracker):

**Supported devices**

- iPhone 11 — *"boots to SpringBoard"*
- iPhone 6s Plus — *"WIP; boots, doesn't restore, no UI"*

**Planned devices** — ARM Macs (M-series), iPads, other/all iPhones, "maybe Apple TVs".

**Supported iOS** — iOS 14.x. **Upcoming** — iOS 13, 15, 16, 17.

**Features listed** — IPSW restore via `libimobiledevice`/`idevicerestore` (through the
companion VM), reverse tethering, display via AppleDisplayPipe (ADP), MultiTouch (MT SPI),
Secure Enclave emulation (SEP ROM + SEP firmware), DART IOMMU, brightness (stub), SmartIO
DMA (SIO), Always-On Processor (AOP).

The page shows three screenshots captioned "iOS Lock Screen in Inferno", "iOS Home Screen in
Inferno" and "iOS System Settings, About in Inferno".

### 4.2 What is corroborated independently of the marketing page

| Evidence | What it proves |
|---|---|
| Issue **#282** (2026-04-08): a user restores iOS 14.0 beta 5 (`18A5351d`) on `-M t8030` and gets an iOS-style panic report containing `OS version: 18A5351d`, `Kernel version: Darwin Kernel Version 20.0.0 … RELEASE_ARM64_T8030`, `iBoot version: ChefKiss Inferno`, `secure boot?: YES`, plus real driver names (`AppleANS2NVMeController`, `AppleT8027USBXDCI`, `AppleS5L8960XWatchDogTimer`, `AppleSMC`, `AppleT8030PMGR`). | The **iOS kernel genuinely executes** inside Inferno, with working NVMe, USB, watchdog, SMC and PMGR device models. |
| The same issue panics in `load_sep_os` with "SEP ROM boot panic". | The boot chain reaches SEP bring-up and can fail there. Booting is not turnkey. |
| `https://chefkiss.dev/guides/inferno/file-setup` | The documented target is `iPhone11,8,iPhone12,1_14.0_18A5351d_Restore.ipsw` — iOS **14.0 beta 5** for **iPhone 11** (`iPhone12,1`). |
| `https://chefkiss.dev/guides/inferno/running-restoring` | Restore is performed by `idevicerestore --erase --restore-mode -i 0x1122334455667788 … -T root_ticket.der`, then stage 2 requires **filesystem patches** and a manual shutdown at the right moment. |
| Device inventory in `hw/arm/apple-silicon/` (`t8030.c`, `t8030-config.c.inc`, `a13.c`, `a13_gxf.c`, `a9.c`, `s8000.c`) | The device claim matches actual code: T8030 (A13/iPhone 11), A9 (iPhone 6s), S8000 (an older SoC) exist; nothing for A14/A15/A16/A17. |
| No releases, guide says "the latest releases are outdated, we must build the tools from source" | There is no packaged, versioned artefact to evaluate. Every user builds `master`. |

### 4.3 What has **not** been demonstrated

- **No iOS 13, 15, 16 or 17 boot.** These are listed as "upcoming" only.
- **No iPhone 6s Plus UI.** The project's own wording is "boots, doesn't restore, no UI".
- **No third-party application execution.** The screenshots show SpringBoard, the home screen
  and Settings → About. Nothing shows an installed App Store app launching, and the restore
  path documented is a *research* kernelcache (`kernelcache.research.iphone12b`), not a
  production configuration.
- **No App Store, iCloud, or DRM-backed service.** SEP emulation exists, but a SEP that boots
  is not the same as a SEP that satisfies Apple's servers.
- **No audio, camera, cellular, Wi-Fi or Bluetooth** in the feature list. Baseband and Wi-Fi
  device models exist in the tree, but no supported-feature claim covers them.

> **Explicit statement.** Neither WinIOS nor this investigation demonstrated genuine iOS
> booting. Inferno's *own* documentation and its users' logs support "iPhone 11 boots iOS 14.x
> to SpringBoard". Treat that as a documented project claim with meaningful corroboration, not
> as something reproduced here.

---

## 5. Windows and macOS compatibility status

### 5.1 Matrix

| Host | CI | Documented | Evidence of real use | Verdict |
|---|---|---|---|---|
| Linux x86_64 | Yes (`build_linux`) | Yes | Many issues from Arch/Ubuntu users | Supported |
| macOS Apple Silicon | Yes (`build_macos`) | Yes, primary | HFV acceleration path | Supported |
| macOS Intel | Yes (`build_macos_x86_64`) | Implied by CI | TCG only | Builds in CI |
| **Windows x86_64 (MSYS2/MinGW)** | **No** | **Yes — PowerShell/CMD tabs in the guide** | **Yes — issues #22, #96, #260, discussion #212** | **Community-supported, fragile** |
| Windows on ARM | No | No | None found | Unverified |
| WSL2 | No explicit CI | Recommended by a maintainer | Issue #282 (Windows 11 + WSL2 + Alpine companion) | Workable |
| MSVC | No | No | None found | Not supported |

### 5.2 What "Windows support" actually consists of

**Positive evidence**

- The host guide's code blocks are tabbed "Non-Windows/WSL/MSYS2", "Windows (Powershell)",
  "Windows (Command Prompt)", including `qemu-img create` commands for the root, firmware,
  syscfg, ctrl_bits, nvram, effaceable, panic_log, sep_nvram and sep_ssc disks.
- The companion-VM guide states the default UNIX socket "is not supported on Windows (not
  including WSL)" and that "connection over IP (`ipv4`/`ipv6` types) was added **solely for
  Windows support**". The `t8030` machine therefore accepts `usb-conn-type=ipv4`,
  `usb-conn-addr`, `usb-conn-port` (see the real command line in issue #282).
- Issue **#22** ("Add Windows support") has a 2025-01-18 maintainer comment: *"clearly works
  (screenshot from @chris-pcguy)"* on branch `feat-sep_emu`, followed by *"companion usb needs
  to have support for non-unix sockets, and then this issue can be closed"*. That follow-up
  work is exactly the `usb-conn-type` TCP path that now exists.
- Discussion **#212** ("How to use it on Windows?"): the asker self-corrects — *"Sorry,
  windows is supported. I should have researched more beforehand."* The maintainer replies:
  *"You can use WSL, but you do have to have a macOS install (or with a lil tinkering just use
  the recovery mode) in order to make the filesystem patches in order for it to boot after the
  1st stage restore."*
- Issue **#260** (2026-03-04, closed 2026-03-05) is the strongest runtime evidence:
  reporter on **Windows 11 25H2, AMD Ryzen 9 7940H** — almost the same class of machine as this
  laptop — hits a compile error in `system/cpus.c`, then after working around it the emulator
  **launches** and aborts with
  `ERROR:../hw/pci/pci.c:2849:pci_device_class_base_init: assertion failed: (conventional || pcie || cxl)`.
  A maintainer replies it does not reproduce on macOS, asks the reporter to try commit
  `8e2eef4`, and the reporter confirms "it's fixed". A crashing launch still proves a working
  Windows binary.
- `os-win32.c` is present at the repository root (2,057 bytes) — QEMU's standard Win32 host
  shim — so the Windows host layer exists, it is just thin and untested by CI.

**Negative evidence**

- `.github/workflows/build.yaml` has exactly three jobs: `build_linux`, `build_macos`,
  `build_macos_x86_64`. No Windows job.
- Issue **#229** "Windows Github CI" — **closed as not planned**.
- Issue **#96** (2025-06-20): Windows 10 22H2 + MinGW64 build fails in
  `hw/arm/apple-silicon/t8030.c:2523` (`variable 'device' set but not used`, with
  `--disable-werror` not honoured as the user expected); **closed as not planned**.
- Issue **#267** "Compilation error on Windows" shows the Windows build repeatedly breaking on
  POSIX-isms in `tcg/region.c` (`PROT_NONE`, `MAP_PRIVATE`, `MAP_ANONYMOUS`, duplicate
  `alloc_code_gen_buffer`), and a maintainer admitting an `#ifdef` had been removed by accident.
  This is the concrete shape of "community-supported": Windows builds regress silently.
- Issue #22's early history: *"Original copy won't work. `bzero` is unavailable on windows and
  there are some other compatibility issues."* A later maintainer summary of that era: *"Chris
  cannot replicate this dram error with latest commit, but emulator also does not run,
  seemingly because of a bug with the kernel allocations (device tree is invalid). also, even if
  we fix this you can't install iOS because the companion uses a UNIX socket."*
- One 2025-07-15 report (#22) says `usbmuxd` starts on Windows but no "Apple USB device"
  appears in `lsusb`, i.e. the virtual USB link is not visible to the companion; another
  contributor attributes it to firewalls/port conflicts. Unresolved, and central to restoring.

**Net assessment:** a Windows build is achievable today with MSYS2/MinGW64 and the right
workarounds, and at least one user has run it on Windows 11 + AMD. It is **not** a supported
configuration, has **no** CI coverage, and the restore path additionally needs macOS (or
improvisation) for filesystem patches. Anyone planning to depend on it should expect to
maintain their own patch set.

### 5.3 macOS status

macOS is the best-supported host: two CI jobs (Apple Silicon and Intel), an HVF acceleration
backend (`accel/hvf`, `target/arm/hvf`, `target/arm/hvf_arm.h`), and the guide's default
platform. Apple Silicon gets hardware acceleration; Intel Macs fall back to TCG like Windows.

---

## 6. Reusable modules (paths, roles, portability)

Everything below lives in a QEMU object model (`QOM`, `MemoryRegion`, `qdev`, `chardev`). None
of it compiles standalone. "Reusable" here means **"portable C that continues to work when
QEMU is built for a Windows host"**, not "copy-pasteable into WinIOS".

### 6.1 Machine and SoC models

| Path | Role | Windows portability |
|---|---|---|
| `hw/arm/apple-silicon/t8030.c` | The `t8030` machine: iPhone 11 (A13) board wiring, NVMe, USB, SEP, PMU, display, buttons | Portable C; the compile error in #96 was in this file |
| `hw/arm/apple-silicon/t8030-config.c.inc` | Generated register/clock configuration tables | Portable |
| `hw/arm/apple-silicon/a13.c`, `a13_gxf.c` | A13 SoC + GXF (GPU) model | Portable; GPU is not accelerated |
| `hw/arm/apple-silicon/a9.c` | A9 / iPhone 6s SoC | Portable, WIP |
| `hw/arm/apple-silicon/s8000.c` | Older SoC | Portable |
| `hw/arm/apple-silicon/boot.c` | Boot trampoline / entry setup | Portable |
| `hw/arm/apple-silicon/dt.c` | Device-tree construction for the guest | Portable |
| `hw/arm/apple-silicon/kernel_patches.c`, `patcher.c` | Guest kernel patching | Portable; **legally sensitive** |
| `hw/arm/apple-silicon/mem.c` | Guest memory layout | Portable |
| `hw/arm/apple-silicon/sart.c` | SART (address translation) | Portable |
| `hw/arm/apple-silicon/dart.c` | DART IOMMU | Portable |
| `hw/arm/apple-silicon/sep.c`, `sep-sim.c` | Secure Enclave / SEP simulation | Portable; requires SEP ROM + firmware the user must supply |
| `hw/arm/apple-silicon/mt-spi.c` | MultiTouch SPI controller | Portable |
| `hw/arm/apple-silicon/lm-backlight.c` | Backlight stub | Portable |

### 6.2 Peripheral models (`hw/misc/apple-silicon/`)

`aes.c`, `aop.c` (+ `a7iop/`), `baseband.c`, `buttons.c`, `chestnut.c`, `fan53740.c`,
`mic_tempsensor.c`, `pmu-d2255.c`, `roswell.c`, `smc.c`, `spmi-baseband.c`, `spmi-pmu.c`,
`tempsensor.c`, plus `meson.build` and trace events.

These are ordinary `qdev` devices. They are the bulk of what makes the iPhone 11 boot, and
they are exactly the code that cannot be lifted into a non-QEMU project.

### 6.3 Target/CPU layer

| Path | Role | Notes |
|---|---|---|
| `target/arm/` (whole directory) | QEMU's AArch64 translator, helpers, sysregs, MMU (`ptw.c`), GDB stub | The actual CPU. Not a reusable library. |
| `target/arm/emulate/aarch64.c` + `.h` | **AGPLv3** fallback emulator for LDP/STP/LDR/STR when an accelerator cannot handle them | Read it as a design example only. Do not copy into WinIOS. |
| `target/arm/vfp_fpscr.c` | FPCR/FPSR/FPSCR state | LGPL-2.1+; illustrates the FP state model WinIOS lacks |
| `target/arm/cpregs-pmu.c`, `cortex-regs.c`, `cpu-sysregs.h.inc` | Project additions to system registers | Evidence of how much sysreg surface iOS needs |
| `fpu/` | QEMU softfloat | Standard QEMU |
| `accel/tcg/` | TCG (the only accelerator usable on x86_64 Windows) | Standard QEMU |

### 6.4 Everything else

- `ui/` — QEMU's display frontends (`cocoa`, `gtk`, `sdl`, `dbus`, ...). `ui/icons/CKBrandingNotice.md`
  adds branding restrictions.
- `system/cpus.c` — the file that broke the Windows build in #260; **not** reusable, but a good
  indicator of Windows fragility.
- `meson.build` (165 KB), `configure` (40 KB), `Makefile` (10 KB) — QEMU's build system.

### 6.5 Verdict on reuse

| Reuse strategy | Feasible? | Why |
|---|---|---|
| Copy individual device files into WinIOS | No | They depend on QOM/qdev/MemoryRegion/chardev; would require porting QEMU's object model first |
| Vendor the whole repository into `D:\WinISO26` | No | ~404 MiB, 12 submodules, meson/ninja, ~5,600 build objects (per the issue #96 log), GPLv3/AGPLv3 |
| Build Inferno separately, drive it from WinIOS over TCP | **Yes** | Clean process boundary, no licence contamination, no build-system collision |
| Use Inferno's source as a *reference* for device behaviour | Yes, with care | Reading GPL code and reimplementing is a legal grey area; do not copy expressions |

---

## 7. Components that cannot be directly reused on Windows

| Component | Path | Why |
|---|---|---|
| HVF accelerator | `accel/hvf/`, `target/arm/hvf/`, `target/arm/hvf_arm.h` | Hypervisor.framework is a macOS API; absent on Windows |
| KVM accelerator | `accel/kvm/`, `target/arm/kvm.c`, `kvm-stub.c`, `kvm-consts.h` | Linux ioctls; and useless on an x86_64 host anyway |
| Xen accelerator | `accel/xen/` | Linux/Xen-only |
| WHPX accelerator | *does not exist* | Never added to this fork |
| POSIX host layer | `os-posix.c`, UNIX-socket chardev code, `umask`/`fork`/`signal` paths | Win32 has no UNIX domain sockets pre-Win10-1803 and the code uses POSIX idioms |
| `usb-tcp-remote`/companion USB | `hw/usb/`, `t8030` `usb-conn-*` properties | Works over TCP, but the Windows `usbmuxd` link is reported broken (#22) |
| Display frontends | `ui/cocoa`, `ui/gtk`, `ui/sdl` | Platform-specific; only `-display none`/VNC/`screendump` are portable |
| Firmware and boot materials | `roms/*` submodules, IPSWs, SEP ROMs, trust caches, SHT tickets | Not distributable; user-supplied |
| Filesystem patch workflow | documented in `https://chefkiss.dev/guides/inferno/fs-patches/` | Maintainer states it needs macOS or "tinkering" (#212) |
| Build system | `configure`, `meson.build`, `Makefile` | Not MSVC/CMake; requires MSYS2 + meson + ninja |

---

## 8. Compatibility gaps and estimated engineering complexity

### 8.1 Gap analysis: what a real iOS boot requires

| Requirement | Inferno status | WinIOS status | Gap for a WinIOS-native path |
|---|---|---|---|
| AArch64 CPU at EL0/EL1/EL2/EL3 | Full QEMU translator + project sysregs | 30-ish scalar instructions, EL0 only, no exceptions | **Complete reimplementation** |
| MMU / page tables / permissions | `target/arm/ptw.c` | None | Complete |
| Exception model, vectors, `ERET`, `SVC` | QEMU | None | Complete |
| FP/SIMD/NEON | Softfloat + translator | **None** | Complete |
| Interrupt controllers (AIC), timers | Device models + QEMU | None | Complete |
| NVMe storage with 4K logical blocks | `nvme-ns` devices, 7 namespaces | None | Complete |
| DART IOMMU | `dart.c` | None | Complete |
| SEP (Secure Enclave) | `sep.c`, `sep-sim.c` | None | Complete, plus firmware you must obtain |
| Display (ADP), MultiTouch MT SPI | Device models, software rendering | None | Complete |
| PMU, SMC, AOP, AES, baseband, tempsensors | ~13 device models | None | Complete |
| Boot chain: iBoot, kernelcache, trust cache, ticket | `boot.c`, `kernel_patches.c`, `patcher.c` | None | Complete |
| IPSW restore pipeline | Companion VM + patched `idevicerestore` | None | Complete, plus external tooling |
| Filesystem patches for stage-2 restore | Documented, partially manual | None | Complete, plus macOS |
| Application launch / frameworks | SpringBoard reached; third-party apps **not demonstrated** | None | Unbounded |
| Performance | TCG, software rendering | n/a | Unbounded |

### 8.2 Order-of-magnitude comparison

| Metric | Inferno | WinIOS |
|---|---|---|
| Source footprint | ~404 MiB repository, ~5,600 build objects | ~1,100 lines of C++ across 5 modules |
| Device models | ~25 Apple-specific | 0 |
| Accelerators | TCG, HVF, KVM (partial), Xen | None (a scalar interpreter) |
| External toolchain | meson, ninja, glib, ~24 packages | CMake, MSVC |
| Test suites | Builds + issue tracker; no in-tree unit suite found | 5 CTest suites, 329 assertions |
| Boots iOS | Yes, one device, one OS family | No |

### 8.3 Effort estimates (relative, not dated)

- **Continuing WinIOS toward an iOS boot:** reimplementing QEMU's AArch64 target plus ~25 device
  models plus the boot chain is the work of a large team over years. This is not a
  "next phase"; it is a different project. The interpreter's current rate of progress
  (a few dozen instructions per phase) makes the distance effectively unbounded.
- **Porting Inferno to a first-class Windows host** (a real CI job, MSVC or a pinned MSYS2
  image, no POSIX regressions, a working `usbmuxd` bridge, an installer): weeks to months for
  someone fluent in QEMU and Win32, with ongoing maintenance because upstream has no Windows CI.
- **Using Inferno as-is on this laptop, with a thin WinIOS launcher:** days to a couple of weeks
  to get a reproducible MSYS2 build and a validated command line; the restore/boot attempt is
  the risky part and is bounded by external factors (firmware, SEP ROM, tickets, filesystem
  patches), not by code.
- **Running arbitrary third-party iOS apps:** not demonstrated by anyone in the evidence
  reviewed. Do not plan around it.

---

## 9. Recommended architecture for WinIOS

### 9.1 Decision

**Do not integrate Inferno into WinIOS. Do not continue the interpreter as an iOS path.**

Reposition WinIOS into two things it can actually be good at:

1. A **Windows-side launcher, supervisor and log/telemetry console** for a separately built
   Inferno.
2. An **AArch64 semantics test bench** — which is exactly what the Phase-12 work already is.

### 9.2 Target layout

```text
D:\WinISO26\
├─ WinIOS\                       (C++20, CMake, MSVC — stays as it is)
│  ├─ src\cpu\, memory\, loader\, debugger\, runtime\   existing, unchanged
│  ├─ src\bridge\                                       NEW: Inferno process bridge
│  │   ├─ command_builder.*      builds & validates `qemu-system-aarch64 -M t8030 ...`
│  │   ├─ process_supervisor.*   CreateProcess + job object + stdout/stderr capture
│  │   ├─ qmp_client.*           TCP JSON client for QMP (status, stop, cont, screendump)
│  │   └─ gdb_client.*           TCP RSP client (read X0..X30, SP, PC, NZCV, memory)
│  └─ tests\unit\bridge_tests.cpp
├─ inferno\                      (separate checkout, separate build, GPLv3 — never linked)
│  └─ build\qemu-system-aarch64.exe, qemu-system-x86_64.exe
└─ InfernoData\                  (user-supplied: disks, IPSW-derived files, tickets)
```

Rules that keep this clean:

- **No source sharing in either direction.** WinIOS never `#include`s Inferno headers and never
  links its objects. Communication is over TCP only (QMP and the GDB remote serial protocol),
  which is portable and already how QEMU is debugged.
- **WinIOS stays licence-free/GPLv3-compatible by choice.** Because there is no linking, WinIOS
  can choose its own licence later without GPL obligations.
- **Inferno is built by MSYS2/MinGW64, not MSVC.** Two toolchains, two build directories, two
  artefacts. Any attempt to force meson/QEMU through MSVC is a false economy.
- **The companion VM is part of the deployment, not of WinIOS.** Ship a documented procedure,
  not code.

### 9.3 Why this beats code integration

- It is the only option compatible with GPLv3/AGPLv3 without relicensing WinIOS.
- It avoids a 404 MiB vendored tree and a second build system inside an MSVC project.
- It converts Inferno's Windows fragility into an operational concern (pin a commit, keep a
  patch file) rather than a compile-time dependency.
- It gives WinIOS's existing CPU/memory/debugger work a **real purpose**: differential
  validation of the interpreter against QEMU's AArch64 execution, at instruction granularity,
  over the GDB stub. That is a legitimate research contribution and it is small.

### 9.4 Anti-goals

- Do not write an AArch64 JIT.
- Do not implement NEON, the MMU, or exception levels in WinIOS.
- Do not embed an IPSW, SEP ROM, trust cache or ticket in the repository.
- Do not claim iOS support in WinIOS's README at any point in this plan.

---

## 10. Staged plan, starting with a small testable milestone

All stages are gated: **do not start a stage until the previous gate passes.** No completion
dates are promised.

### Stage 0 — Read-only recon (smallest testable milestone)

Goal: prove Inferno builds and runs *something* on this laptop, without any Apple firmware.

1. Install MSYS2; `pacman -S mingw-w64-x86_64-{gcc,ninja,meson,pkgconf,glib2,pixman,nettle,gnutls,libslirp,libusb,lzo,snappy,zstd,lzfse,ncurses,jpeg-turbo,libpng,dtc,python}`.
2. Shallow-clone Inferno at a **pinned commit** (record the SHA) with `--recurse-submodules`.
3. Configure with `--target-list=aarch64-softmmu --disable-werror --disable-guest-agent` only.
   Record the full configure output, especially the accelerator report.
4. Run `qemu-system-aarch64 -M help` and `-M t8030,help`, and capture the property list
   (`trustcache`, `ticket`, `sep-fw`, `sep-rom`, `kaslr-off`, `usb-conn-type`, `usb-conn-addr`,
   `usb-conn-port`).
5. Write the results into `docs/progress.md`.

**Gate:** a `qemu-system-aarch64.exe` exists, `-M t8030` is listed, and the property list
matches §4.2. If the build fails, record the exact error and stop — do not patch blindly.
**No IPSW, no SEP ROM, no ticket at this stage.**

### Stage 1 — WinIOS bridge skeleton (pure C++, fully testable offline)

- `command_builder`: builds an `-M t8030` argument vector from typed fields; rejects unknown
  properties, missing required files, and malformed `usb-conn-*` values.
- `process_supervisor`: `CreateProcessW` + job object (kill-on-close) + redirected pipes;
  handles early exit, timeout, and orphan cleanup.
- Unit tests using a **stub executable** (the existing `win_ios.exe` or a tiny echo binary),
  not Inferno.

**Gate:** all existing suites plus the new bridge suite pass under `/W4 /permissive-`; the
default demo output is unchanged.

### Stage 2 — QMP client and boot-log triage

- Connect to `-qmp tcp:127.0.0.1:<port>,server=on,wait=off`.
- Implement `qmp_capabilities`, `query-status`, `stop`, `cont`, `screendump`, `quit`.
- Parse serial output into events: kernel start, panic (the `panic(cpu …)` shape from #282),
  `load_sep_os` failure, watchdog reset, SpringBoard-ready.

**Gate:** the tool can pause/resume a trivial QEMU guest and classify a synthetic panic log
correctly.

### Stage 3 — Companion VM and TCP USB link

- Build `qemu-system-x86_64` from the same Inferno checkout.
- Run the companion with `-M q35 -m 1G -smp 2 -usb -device usb-ehci …` and the documented
  `usb-tcp-remote` wiring; main VM uses `usb-conn-type=ipv4`.
- Verify the companion sees a device (`lsusb` inside it, `idevicerestore -l`).

**Gate:** the virtual iPhone is discoverable by `idevicerestore` **before** any restore is
attempted. This is the step that failed for the reporter in #22; expect to debug firewalls and
port binding.

### Stage 4 — Restore (external, user-supplied firmware)

Prerequisites the user must supply and legally hold: a supported IPSW (iOS 14.x for
`iPhone12,1`), the matching `n104` device tree and research kernelcache, a SEP ROM image
(`Cebu B1`), and a generated `root_ticket.der`.

- Run restore stage 1 with patched `idevicerestore`.
- Apply the documented filesystem patches (the macOS-or-tinkering step from #212).
- Boot to SpringBoard.

**Gate:** a screenshot of SpringBoard produced by *this* machine, captured via QMP
`screendump`. Anything short of that is not a boot.

### Stage 5 — Measurement and (only then) application experiments

- Measure TCG throughput on the Ryzen 5 4600H (boot time, idle CPU, interactive latency).
- Only if Stage 4 passes: attempt one preinstalled system app, then one sideloaded app. Expect
  this to fail; there is no upstream evidence that it works.

**Abort criteria** (any one stops the programme): Stage 0 fails and needs deep QEMU patching;
Stage 3 cannot be made to work on Windows; Stage 4 requires circumventing a security control;
or legal review rejects the SEP/IPSW workflow.

---

## 11. Sources for externally verified claims

| # | Source | Used for | Access |
|---|---|---|---|
| S1 | `https://api.github.com/repos/ChefKissInc/Inferno` | identity, parent, size, stars, licence metadata, dates | Direct, 2026-10-29 |
| S2 | `https://raw.githubusercontent.com/ChefKissInc/Inferno/master/README.md` | self-description as a QEMU derivative, disclaimers | Direct |
| S3 | `https://raw.githubusercontent.com/ChefKissInc/Inferno/master/LICENSE` | GPLv3 + AGPLv3 split, branding, GPLv2-only files to be removed | Direct |
| S4 | `https://raw.githubusercontent.com/ChefKissInc/Inferno/master/.github/workflows/build.yaml` | CI jobs = Linux, macOS arm64, macOS Intel; no Windows | Direct |
| S5 | `https://raw.githubusercontent.com/ChefKissInc/Inferno/master/.gitmodules` | submodule set | Direct |
| S6 | `https://api.github.com/repos/ChefKissInc/Inferno/releases` | no releases (`[]`) | Direct |
| S7 | `https://api.github.com/repos/ChefKissInc/Inferno/contents/hw/arm/apple-silicon` | device inventory | Direct |
| S8 | `https://api.github.com/repos/ChefKissInc/Inferno/contents/hw/misc/apple-silicon` | peripheral inventory | Direct |
| S9 | `https://api.github.com/repos/ChefKissInc/Inferno/contents/target/arm` | translator/sysreg/FP files | Direct |
| S10 | `https://api.github.com/repos/ChefKissInc/Inferno/contents/accel` | **no `whpx` directory** | Direct |
| S11 | `https://raw.githubusercontent.com/.../target/arm/emulate/aarch64.c` | AGPLv3 header, four-instruction fallback emulator | Direct |
| S12 | `https://raw.githubusercontent.com/.../target/arm/vfp_fpscr.c` | FPCR/FPSR handling, LGPL-2.1+ header | Direct |
| S13 | `https://api.github.com/repos/ChefKissInc/Inferno/issues/22` + `/comments` | Windows history, MSYS2, `bzero`, UNIX socket blocker, 2025 "clearly works" | Direct |
| S14 | `https://api.github.com/repos/ChefKissInc/Inferno/issues/87` | KVM open, milestone Odysseus, AArch64-host-only | Direct |
| S15 | `https://api.github.com/repos/ChefKissInc/Inferno/issues/96` | Windows 10 + MinGW build failure, closed not planned | Direct |
| S16 | `https://api.github.com/repos/ChefKissInc/Inferno/issues/229` | Windows CI closed not planned | Direct |
| S17 | `https://api.github.com/repos/ChefKissInc/Inferno/issues/260` + `/comments` | Win11 25H2 + Ryzen 9 7940H launch + crash, fixed in `8e2eef4` | Direct |
| S18 | `https://api.github.com/repos/ChefKissInc/Inferno/issues/267` + `/comments` | recurring Windows compile breakage in `tcg/region.c` | Direct |
| S19 | `https://api.github.com/repos/ChefKissInc/Inferno/issues/282` | real `t8030` command line, iOS 14.0b5 kernel running, SEP panic | Direct |
| S20 | `https://chefkiss.dev/applehax/inferno` | supported devices, iOS 14.x, planned versions, feature list, screenshots | **403 direct; content via search index** |
| S21 | `https://chefkiss.dev/guides/inferno/` (setup + host-setup + file-setup + running-restoring + companion-setup + fs-patches) | 6 GB/32 GB prerequisites, Windows tabs, TCP-for-Windows, IPSW choice, restore steps | **403 direct; content via search index** |
| S22 | `https://github.com/ChefKissInc/Inferno/discussions/212` | "windows is supported"; macOS needed for filesystem patches | Via search index, 2026-10-29 |
| S23 | `https://theapplewiki.com/wiki/QEMUAppleSilicon` | community description of lineage/rewrite | Via search index |
| S24 | `https://updates.cdn-apple.com/.../iPhone11,8,iPhone12,1_14.0_18A5351d_Restore.ipsw` | the specific IPSW named by the guide (not downloaded) | Not downloaded |

Note on S20/S21: `chefkiss.dev` returns HTTP 403 to non-browser clients, so those pages could
not be read directly. Their content is quoted here from search-engine excerpts, which is why
every claim sourced from them is treated as a **project claim** rather than a measured fact.

---

## 12. Explicit unknowns and unverified claims

1. **Inferno was not built or run.** No code was cloned, no binary was executed, no IPSW was
   downloaded. Every Windows-runtime statement rests on issue reports (#22, #260), not on
   observation here.
2. **The screenshots on `chefkiss.dev` were not independently validated.** They may predate the
   current `master`. What build produced them is unknown.
3. **The "boots to SpringBoard" claim was not reproduced.** The strongest independent support is
   the iOS kernel log in #282, which is a *panic*, not a successful boot.
4. **No third-party iOS application execution is demonstrated anywhere in the evidence
   reviewed.** Do not assume it works, even after a successful boot.
5. **iOS 13, 15, 16 and 17 are planned only.** No evidence of any of them booting.
6. **iPhone 6s Plus status is "boots, doesn't restore, no UI"** — a project statement, not
   verified.
7. **WHPX's absence was verified for `master` only.** An older branch or fork might have carried
   an `accel/whpx`; this was not investigated and does not change the conclusion (an x86_64 host
   cannot accelerate an AArch64 guest).
8. **The precise guest RAM and vCPU requirements** are inferred from the guide's 6 GB host
   minimum and the `-smp 6 -m 4G` example in #282. The machine's own defaults were not read.
9. **Windows restore reliability is unknown.** The one detailed Windows restore report (#282)
   used WSL2 with an Alpine companion and panicked in `load_sep_os`. Whether native MSYS2
   restore works end-to-end is untested.
10. **The `usbmuxd`-on-Windows question is unresolved** (#22, 2025-07-15): one report says the
    virtual device never appears; a contributor blames firewalls or port conflicts.
11. **Performance on this laptop is unmeasured.** TCG on a 6-core Zen 2 will be far slower than
    native; "far slower" is not quantified here.
12. **No legal review has been performed.** Obtain independent advice on IPSW use, SEP ROM
    dumps, `idevicerestore` patching, trust-cache generation, and GPLv3/AGPLv3 obligations
    before distributing anything. This report is not legal advice, and nothing here authorises
    bypassing any security control.
13. **The WinIOS worktree was dirty throughout** (`src/cpu/cpu.cpp`, `tests/unit/cpu_tests.cpp`
    modified before this investigation began). Those changes were preserved untouched, and the
    report was **not committed**, per the instruction to commit only when the tree is clean.
14. **WinIOS has no licence file.** That is a pre-existing gap that becomes material the moment
    GPLv3/AGPLv3 code enters the picture.

---

## Appendix A — WinIOS verification log (this session)

All commands were run from `D:\WinISO26` on 2026-10-29. Existing source files were not modified
by this investigation.

### A.1 Repository state

```text
$ git -C WinIOS status --short
 M src/cpu/cpu.cpp
 M tests/unit/cpu_tests.cpp
```

HEAD: `830e73e Add AArch64 arithmetic and conditional branch instructions`.
The two modified files are the user's in-progress Phase-12 work and were left untouched.

### A.2 Configure

```text
$ cmake -S WinIOS -B WinIOS\build -G "Visual Studio 18 2026" -A x64
-- Selecting Windows SDK version 10.0.26100.0 to target Windows 10.0.26300.
-- Configuring done (0.4s)
-- Generating done (0.7s)
-- Build files have been written to: D:/WinISO26/WinIOS/build
```

### A.3 Build

```text
$ cmake --build WinIOS\build --config Debug
  winios_core.vcxproj -> D:\WinISO26\WinIOS\build\Debug\winios_core.lib
  cpu_tests.vcxproj -> D:\WinISO26\WinIOS\build\Debug\cpu_tests.exe
  debugger_tests.vcxproj -> ...\debugger_tests.exe
  loader_tests.vcxproj -> ...\loader_tests.exe
  memory_tests.vcxproj -> ...\memory_tests.exe
  runtime_tests.vcxproj -> ...\runtime_tests.exe
  win_ios.vcxproj -> D:\WinISO26\WinIOS\build\Debug\win_ios.exe
```

No warnings were emitted under `/W4 /permissive-`.

### A.4 CTest

```text
$ ctest --test-dir WinIOS\build -C Debug --output-on-failure
1/5 Test #1: memory_tests .....................   Passed    0.04 sec
2/5 Test #2: cpu_tests ........................   Passed    0.04 sec
3/5 Test #3: debugger_tests ...................   Passed    0.04 sec
4/5 Test #4: loader_tests .....................   Passed    0.07 sec
5/5 Test #5: runtime_tests ....................   Passed    0.07 sec

100% tests passed out of 5
Total Test time (real) =   0.39 sec
```

Assertion counts reported by the suites themselves:

| Suite | Assertions |
|---|---|
| `memory_tests` | 53 |
| `cpu_tests` | 173 |
| `debugger_tests` | 28 |
| `loader_tests` | 34 |
| `runtime_tests` | 41 |
| **Total** | **329** |

### A.5 Default demo (unchanged)

```text
$ .\WinIOS\build\Debug\win_ios.exe
WinIOS v0.1.0
ARM64 emulation research prototype
Demo calculation: X2 = 30
CPU state demonstration complete.
exit=0
```

### A.6 Raw-binary fixture

With the completion sentinel:

```text
$ .\WinIOS\build\Debug\win_ios.exe --run-binary .\WinIOS\examples\test_program.bin `
    --memory-size 65536 --entry 0 --max-instructions 5 --sentinel 24
Status: completed
Loaded bytes: 20
Instructions executed: 5
Final PC: 0x18
X0: 47
X1: 256
SP: 65536
Memory[0x100]: 47
exit=0
```

Without the sentinel:

```text
$ ... --max-instructions 5
Status: instruction_limit_reached
...
exit=2
```

### A.7 Invalid input

```text
$ .\WinIOS\build\Debug\win_ios.exe --run-binary .\WinIOS\examples\missing.bin `
    --memory-size 4096 --entry 0 --max-instructions 5
Status: load_error
Loaded bytes: 0
Execution error: Unable to open raw binary: .\WinIOS\examples\missing.bin
exit=1
```

### A.8 Source inventory (excluding `build/` and `.git/`)

| File | Lines |
|---|---|
| `src/cpu/cpu.cpp` | 663 |
| `src/cpu/cpu.h` | 58 |
| `src/debugger/debugger.cpp` | 57 |
| `src/debugger/debugger.h` | 18 |
| `src/loader/binary_loader.cpp` | 43 |
| `src/loader/binary_loader.h` | 17 |
| `src/memory/memory.cpp` | 63 |
| `src/memory/memory.h` | 25 |
| `src/runtime/guest_runtime.cpp` | 130 |
| `src/runtime/guest_runtime.h` | 44 |
| `src/main.cpp` | 148 |
| `tests/unit/cpu_tests.cpp` | 512 |
| `tests/unit/debugger_tests.cpp` | 125 |
| `tests/unit/loader_tests.cpp` | 112 |
| `tests/unit/memory_tests.cpp` | 97 |
| `tests/unit/runtime_tests.cpp` | 181 |
| `tests/unit/test_harness.h` | 60 |

---

## Appendix B — Evidence levels used in this report

| Level | Meaning |
|---|---|
| **Measured** | Reproduced by a command in Appendix A, on this machine, in this session. |
| **Primary** | Read directly from the canonical source (GitHub API, raw file, issue thread) on 2026-10-29. |
| **Corroborated** | A project claim plus at least one independent artefact (a user log, a source file, a CI job). |
| **Claim** | Stated by the project or its maintainers, not independently reproduced. |
| **Unverified** | Nothing found; listed in §12. |

Mapping of the main conclusions:

| Conclusion | Level |
|---|---|
| Inferno repository identity, size, licence metadata | Primary |
| GPLv3 + AGPLv3 split | Primary |
| No releases; must build from source | Primary |
| CI covers Linux + macOS only | Primary |
| No WHPX backend in `accel/` | Primary |
| KVM unimplemented (open issue, milestone) | Primary |
| iPhone 11 boots iOS 14.x to SpringBoard | Corroborated / Claim |
| iOS kernel executes inside Inferno | Corroborated (issue #282 panic log) |
| Windows builds and launches, community-supported | Corroborated (issue #260, guide tabs, #22) |
| Windows CI rejected | Primary |
| No third-party app execution | Unverified (absence of evidence) |
| WinIOS cannot boot iOS | Measured by inspection / Claim by the project itself |
