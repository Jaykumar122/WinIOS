# WinIOS Campaign 228: Capability & Boundary Matrix

## 1. Subsystem Capability Analysis

| Subsystem | Implemented Real Capability | Research / Simulated Boundary | Non-Goals / Disclosed Limitations |
|---|---|---|---|
| **System State Management** | Full transactional in-memory state engine, typed bounds checks, observer notification, VFS serialization | VFS persistence writes to virtual in-memory filesystem `/System/Library/Preferences/system_settings.ini` | Not a replacement for Apple `CFPreferences`, `NSUserDefaults`, or `defaults` daemon |
| **Appearance & Theming** | Dynamic Light and Dark palettes, 8 system accent colors, high-contrast monochrome palette override | Palette colors applied to software compositor drawing routines and guest window decorations | No Metal/OpenGL/Vulkan hardware GPU shader pass or dynamic blur materials |
| **Display & Orientation** | Resolution selection, scale selection (100% to 200%), orientation switching (Landscape / Portrait) | Virtual software display framebuffer (default 800x600 32bpp) | Does not reconfigure physical host monitor or change native Windows display resolution |
| **Audio Configuration** | Master volume clamping (0..100%), mute toggling, output device query | Audio subsystem maintains simulated frame queues and underrun counters | Zero physical audio playback; no WASAPI or direct host speaker output |
| **Power & Battery** | Battery level percentage, charging state metadata, simulated low power mode | Simulated virtual power state machine | No connection to physical laptop battery or ACPI power rail |
| **Network Control** | Interface enable/disable, virtual statistics (bytes/packets sent/recv) | Isolated virtual loopback adapter | Absolute network isolation: NO host socket access, NO external internet routing |
| **Control Center** | Interactive quick-control card (320x360), pointer routing, outside-click & Escape dismissal | Renders inside software compositor desktop layer | Not Apple SpringBoard / iOS Control Center; bespoke research desktop UI |
| **Task Management** | Unified process identity, task switcher overlay cycling, graceful & force kill | Connects to `BsdProcessManager` and `UserSyscallDispatcher` | No Darwin Mach port right transfer or launchd XPC daemon |
| **Accessibility** | Semantic node registry, circular focus traversal (`focus_next`/`prev`), text scaling (1.0x-2.0x), high-contrast | Node metadata registered programmatically by UI elements | Not Apple VoiceOver; no audio screen reader text-to-speech synthesis |
| **Guest Architecture** | Freestanding ARM64 Mach-O execution via CPU emulator and UserAddressSpace MMU translation | Cross-compiled on Windows 11 host using Zig 0.17.0 | Not iOS, UIKit, or SwiftUI; freestanding C runtime linked with `winios_runtime.c` |

---

## 2. Security Boundaries & Invariants

- **Isolation:** Network traffic remains 100% virtual loopback. No host sockets, internet access, or outbound packets are permitted.
- **Process Boundaries:** Tasks cannot terminate arbitrary processes without valid PID ownership or supervisor capability.
- **Buffer Safety:** All syscall copy-in and copy-out operations strictly validate user pointers through `UserAddressSpace` page tables.
- **Clean Invariants:** All 4 repository protected assets retain 100% bit-for-bit integrity.
