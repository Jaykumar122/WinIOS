# WinIOS Guest Graphics & Rendering API Report

**Campaign:** 221 (Stage 221.14)  
**Subsystem:** Display Surface Updates & Presentation  

---

## 1. Application-Owned Drawing Surfaces

Each window is backed by a 32-bit ARGB virtual framebuffer:
- `winios_surface_fill(wid, x, y, width, height, color)`: Accelerated rectangle fills directly in surface memory.
- `winios_surface_update(wid, pixels, x, y, width, height)`: Copies a pixel buffer into the window surface.
- `winios_surface_present(wid)`: Submits the updated surface to the compositor, generating a damage region and updating the host presentation display.

---

## 2. Distinction from Host Rendering

- **Guest Side:** The guest application issues simple 2D drawing and fill commands through standard WinIOS Category C syscalls (502, 503, 505).
- **Host Side:** The host window server composites dirty regions using GDI or Direct3D11 backends. WinIOS explicitly does NOT claim Metal or CoreAnimation emulation.
