# Campaign 222 — Stage 222.5 Guest Surface & Framebuffer Contract Report

## Objectives & Achievements
Stage 222.5 establishes and verifies the formal contract for guest surfaces and backing framebuffers, covering stride alignment, ARGB8888 pixel formats, dirty-region damage accumulation, process access control, and allocation quotas.

## Key Technical Specifications

### 1. Framebuffer Stride & Geometry
- **Format**: 32-bit ARGB8888 (`PixelFormat::B8G8R8A8_UNORM` / `R8G8B8A8_UNORM`), 4 bytes per pixel.
- **Stride**: Stride is strictly validated:
  $$\text{stride} = \text{width} \times 4$$
- **Buffer Size**: Guaranteed to be at least $\text{stride} \times \text{height}$ bytes. Dynamic resizing updates dimensions, stride, reallocates buffer space, and marks full damage.

### 2. Damage Tracking (`DamageTracker`)
- Sub-region damage is recorded via `add_dirty_rect(Rect)`.
- The aggregate damage bounding box dynamically expands to enclose all modified regions.
- Initial allocation or resize operations invoke `mark_full_damage()`.
- Upon compositor presentation, `clear()` resets the dirty rectangle list.

### 3. Access Control & Boundary Validation
- Each surface records its creator PID (`owner_pid`).
- Calls to `write_pixel`, `read_pixel`, `fill_rect`, and `copy_pixels` verify caller credentials. Unprivileged access attempts return `SurfaceResultCode::AccessDenied`.
- Operations completely outside surface bounds return `SurfaceResultCode::OutOfBounds`.

### 4. Quota Enforcement & Lifecycle Management
- `SurfaceResourceManager` enforces:
  - Max bytes per surface: 32 MB (`kMaxSurfaceBytes`).
  - Max surfaces per process: 16 (`kMaxSurfacesPerProcess`).
  - Global memory budget: 128 MB (`kMaxTotalDisplayBudget`).
- Terminated process cleanup (`cleanup_process_surfaces`) reclaims all surface memory and zeroes process accounting counters.

## Test Verification
All tests in `tests/unit/stage222_5_guest_surface_tests.cpp` passed 100%:
- `test_surface_geometry_and_stride`: PASS
- `test_surface_damage_tracking`: PASS
- `test_surface_access_control_and_pixel_ops`: PASS
- `test_surface_resource_manager_quotas_and_cleanup`: PASS
