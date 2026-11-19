# Developer Toolchain & Reproducible Build Guide

## 1. Toolchain Requirements & Host Environment

Building and packaging WinIOS guest applications requires:
- **Host Operating System**: Windows 11 x64 (Build 26100+)
- **Host Compiler**: Microsoft Visual C++ 2022 (v144 toolset), MSVC C++20 standard, strictly adhering to `/W4 /WX /permissive-`.
- **Build System**: CMake 3.31.6+
- **Guest Cross-Compiler**: Zig 0.17.0 targeting `aarch64-freestanding`.

---

## 2. Canonical Packaging & Bit-for-Bit Determinism

WinIOS packages (`.wiospkg`) achieve 100% reproducible, bit-identical binary output across diverse build machines and environments by enforcing canonicalization at every level:

### Deterministic Packaging Rules
1. **Lexicographical File Sorting**: Files inside the package archive are sorted strictly by their normalized relative VFS path string (`std::sort` over UTF-8 POSIX paths).
2. **Path Normalization**: All directory separators are converted to `/`. Redundant slashes and dot-segments (`./`, `../`) are eliminated before serialization.
3. **UTC Timestamps / Zeroed Metadata**: Container headers omit host-dependent timestamps, user IDs, group IDs, and file modification times.
4. **Deterministic Signatures**: HMAC-SHA256 signature trailers are computed over the canonical container byte stream, ensuring that two builds with identical source files and identical keys produce identical byte streams.

---

## 3. Developer CLI Workflow (`winios-cli`)

The `winios-cli` provides an end-to-end command-line workflow for guest development:

### 1. Generate Application Skeleton
```powershell
winios-cli app-gen --bundle-id com.developer.notes --name "QuickNotes" --output-dir ./src/apps/notes
```
Generates C source targeting SDK v2.4 (`0x00020400`), manifest `manifest.ini`, and Zig build scripts.

### 2. Inspect Mach-O Binary
```powershell
winios-cli app-inspect --binary ./bin/notes.macho
```
Validates ARM64 64-bit Mach-O headers, 16 KB page granule alignment, and W^X cleanliness.

### 3. Build Canonical Package
```powershell
winios-cli app-pack --manifest ./src/apps/notes/manifest.ini --output ./build/notes.wiospkg
```

### 4. Digitally Sign Package
```powershell
winios-cli app-sign --package ./build/notes.wiospkg --publisher-id platform-root
```
Appends authenticated `WIOSAUTH` trailer with HMAC-SHA256 signature.

### 5. Verify Package Authenticity
```powershell
winios-cli app-verify --package ./build/notes.wiospkg
```
Checks signature integrity and trust store validity.

### 6. Install Application
```powershell
winios-cli app-install --package ./build/notes.wiospkg
```
Installs the package transactionally with crash-consistent staging and manifest verification.

### 7. Launch and Debug Application
```powershell
winios-cli app-launch --bundle-id com.developer.notes
winios-cli app-status --pid 101
winios-cli app-terminate --pid 101
```

---

## 4. Truthful Diagnostics & Profiling

The developer profiler tracks retired guest instructions, emulation ticks, and virtual memory allocations truthfully:
- Guest instruction counts represent simulated interpreter execution steps, not physical host CPU performance counters.
- Memory tracking clamps deltas to zero to prevent unsigned arithmetic underflow.
