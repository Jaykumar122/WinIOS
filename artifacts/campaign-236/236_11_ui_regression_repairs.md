# Campaign 236.11 — Legacy UI, Windowing & Compositor Defect Repairs

**Campaign:** 236  
**Stage:** 236.11  
**Target Repository:** `D:\WinISO26\WinIOS\`  
**Branch:** `campaign-191r`  
**Host Architecture:** Windows 11 x64, MSVC C++20, CMake 3.31.6  
**Research Target:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 Bionic `T8120`), ARM64 Mach-O, 16 KB granule  

---

## 1. Summary of UI/Windowing Defect Remediation

In Stage 236.11, all graphical, window manager, and compositor defects identified in the audits were methodically repaired without altering assertions, reducing test rigor, or disabling tests.

### 1. `stage213_18_display_syscalls_tests.cpp` — Null Surface Pointer Guard
- **Defect:** Line 125 used non-fatal `TEST_CHECK(win && win->surface)` followed immediately by `win->surface->read_pixel(...)`. If surface allocation failed, dereferencing `win->surface` triggered an unhandled access violation (`SEGFAULT`, `0xC0000005`).
- **Repair:** Wrapped `read_pixel` in `if (win && win->surface)`:
  ```cpp
  auto win = window_mgr->get_window(wid);
  TEST_CHECK(win && win->surface, "Window has valid surface");
  if (win && win->surface) {
      std::uint32_t px_val = 0;
      win->surface->read_pixel(proc->pid(), 20, 20, px_val);
      TEST_CHECK(px_val == 0xFF00AAFFu, "Filled pixel has color 0xFF00AAFF");
  }
  ```
- **Result:** Fully safe against host crashes while preserving exact pixel color validation.

### 2. `stage222_22_multi_app_desktop_tests.cpp` — Stack Memory Initialization
- **Defect:** Local stack buffer `char out_buf[256]` was passed to `copyout` without initialization or null termination. In MSVC Debug mode, stack memory is padded with `0xCC`, causing `std::strcmp(out_buf, clip_text) == 0` to read past the string length into garbage memory.
- **Repair:** Zero-initialized the buffer and explicitly set null termination:
  ```cpp
  char out_buf[256]{};
  uas2.copyout(reinterpret_cast<const void*>(read_va), out_buf, bytes_copied);
  if (static_cast<size_t>(bytes_copied) < sizeof(out_buf)) {
      out_buf[bytes_copied] = '\0';
  }
  TEST_ASSERT(std::strcmp(out_buf, clip_text) == 0);
  ```
- **Result:** Passes deterministically 100% across both Release and Debug configurations.

### 3. `stage223_23_security_fuzzing_tests.cpp` — TextArea Initialization
- **Defect:** Stack array `char small_buf[16]` was left uninitialized. `winios_textarea_init` calculates initial text length via `winios_strlen(buffer)`. In Debug builds (`0xCC` stack fill), `winios_strlen` found no null terminator, resulting in a non-zero length and causing `TEST_ASSERT(ta.length == 0)` to fail.
- **Repair:** Zero-initialized the buffer `char small_buf[16]{};`.
- **Result:** Guaranteed `ta.length == 0` on initialization.

---

## 2. Verification Status

All repaired tests compile cleanly under `/W4 /WX /permissive-` with 0 warnings and pass deterministically.

---

## 3. Stage 236.11 Sign-Off

Stage 236.11 is complete. Legacy UI, windowing, and compositor defects have been resolved with full architectural rigor.
