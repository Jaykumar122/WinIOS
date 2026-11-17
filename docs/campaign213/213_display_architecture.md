# WinIOS Campaign 213 — Virtual Display Architecture Specification

## 1. Overview and Engineering Profile

WinIOS Campaign 213 establishes a bounded, high-fidelity virtual display and windowing architecture targeting the iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`) research profile.

The virtual display subsystem operates as a Category C/D architectural component, bridging genuine ARM64 userspace processes executing at EL0 to a Windows 11 host presentation surface without fabricating private Apple firmware or Metal GPU equivalence.

## 2. Geometry and Display Configuration

The default display geometry models the iPhone 15 Super Retina XDR portrait display profile while retaining dynamic runtime reconfigurability:

| Parameter | Default Value | Description |
| :--- | :--- | :--- |
| **Native Width** | 1,179 pixels | Panel horizontal resolution |
| **Native Height** | 2,556 pixels | Panel vertical resolution |
| **Display Scale** | 3.0x | Retina pixel density factor |
| **Orientation** | Portrait | Supports Portrait, LandscapeLeft, PortraitUpsideDown, LandscapeRight |
| **Pixel Format** | `B8G8R8A8_UNORM` | 32-bit BGRA (Little-Endian Little ARGB on Windows) |
| **Bytes Per Pixel** | 4 bytes | 32 bits per pixel |
| **Row Stride** | 4,716 bytes | $1,179 \times 4$ bytes (cache-aligned) |
| **Total Allocation** | 12,054,096 bytes | $\approx 11.5$ MB frame buffer backing |
| **Refresh Interval** | 16,666 $\mu$s | Deterministic 60 Hz virtual clock period |
| **Max Allocation Cap**| 67,108,864 bytes | 64 MB security quota |
| **Guest Physical Addr**| `0x240000000` | Stage-2 mapped physical framebuffer address |

### Dynamic Orientation Support
When switching orientation between portrait and landscape:
$$\text{Width} \longleftrightarrow \text{Height}$$
$$\text{RowStride} = \text{Width} \times \text{BytesPerPixel}$$
All operations validate integer multiplication overflow and buffer size constraints prior to reconfiguration.

## 3. Pixel Formats and Channel Semantics

The subsystem supports three deterministic pixel representations:

1. **`B8G8R8A8_UNORM` (Default)**:
   - Byte 0: Blue (8-bit unsigned normalized)
   - Byte 1: Green (8-bit unsigned normalized)
   - Byte 2: Red (8-bit unsigned normalized)
   - Byte 3: Alpha (8-bit unsigned normalized)
   - Native format for Windows GDI / DIB / DirectX presentation.

2. **`R8G8B8A8_UNORM`**:
   - Byte 0: Red, Byte 1: Green, Byte 2: Blue, Byte 3: Alpha.

3. **`R5G6B5_UNORM`**:
   - Standard 16-bit packed RGB representation (5 bits red, 6 bits green, 5 bits blue).

### Rasterization and Porter-Duff Alpha Compositing
Alpha blending implements deterministic Porter-Duff Source-Over compositing:
$$A_{out} = A_{src} + \frac{A_{dst} \cdot (255 - A_{src})}{255}$$
$$C_{out} = \frac{C_{src} \cdot A_{src} + C_{dst} \cdot (255 - A_{src})}{255}$$
Integer arithmetic with half-up rounding ($+127 / 255$) ensures bit-exact reproducible results across Debug and Release builds.

## 4. Multi-Surface Architecture and Process Isolation

Each guest application owns an isolated `DisplaySurface` managed by `SurfaceResourceManager`:
- **Ownership Verification**: Every read, write, fill, and copy operation checks `caller_pid == owner_pid || caller_pid == 0`. Cross-process writes return `SurfaceResultCode::AccessDenied`.
- **Memory Boundaries**: All buffer offsets are checked against `buffer.size()`. Out-of-bounds accesses return `SurfaceResultCode::OutOfBounds`.
- **Resource Quotas**:
  - Maximum bytes per surface: 32 MB (`kMaxSurfaceBytes`).
  - Maximum surfaces per process: 16 surfaces (`kMaxSurfacesPerProcess`).
  - Total display memory budget: 128 MB (`kMaxTotalDisplayBudget`).
  - Atomic rollback on quota exhaustion.
  - Automatic reclamation of all surface memory on process termination via `cleanup_process_surfaces(pid)`.

## 5. Compositing Pipeline and Damage Tracking

`Compositor` manages the multi-layer visual stack:
1. Surfaces are aggregated and sorted by Z-order ascending (back to front).
2. Damage tracking (`DamageTracker`) maintains dirty rectangles.
3. Partial redraw recompiles only the damage bounding box, leaving clean areas intact.
4. Final composed pixels are delivered to `DirectXPresentationAdapter` and `DisplayWindow`.
