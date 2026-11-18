# WinIOS Windows-Hosted Developer Workflow & Tooling

**Campaign:** 221 (Stage 221.19)  
**Host Platform:** Windows 11 x64  
**Cross Compiler:** Zig 0.17.0 (`zig cc -target aarch64-macos -nostdlib`)  
**Package Tool:** `guest-sdk/tools/wios_packager.py`  

---

## 1. End-to-End Developer Workflow

The WinIOS developer workflow allows Windows developers to create, build, validate, package, inspect, verify, and launch native ARM64 guest applications without needing macOS, Xcode, WSL, or an external device.

### 1.1 Project Initialization
```powershell
python WinIOS/guest-sdk/tools/wios_packager.py init MyCalcApp --id com.winios.calc --name "Calculator"
```
Generates standard project hierarchy:
- `MyCalcApp/src/main.c`: Entry point using `<winios/winios.h>`.
- `MyCalcApp/manifest.txt`: Package Format v2 manifest specifying permissions and quotas.
- `MyCalcApp/res/`: Directory for icons, bitmaps, and configs.

### 1.2 Freestanding ARM64 Compilation
```powershell
python WinIOS/guest-sdk/tools/wios_packager.py build MyCalcApp --sdk-dir WinIOS/guest-sdk -o calc_arm64.macho
```
Directly invokes Zig to produce an authentic AArch64 Mach-O binary linked against `crt0.s` and `winios_runtime.c`.

### 1.3 Packaging into `.wios` Container (v2.0)
```powershell
python WinIOS/guest-sdk/tools/wios_packager.py pack MyCalcApp -o calc.wios
```
Serializes the manifest and computes SHA-256 integrity digests for all payload files into the versioned binary container (`WIOSPKG2`).

### 1.4 Container Inspection & Integrity Verification
```powershell
python WinIOS/guest-sdk/tools/wios_packager.py inspect calc.wios
python WinIOS/guest-sdk/tools/wios_packager.py verify calc.wios
```
Performs security auditing (ensuring no `..` or root traversal attacks) and verifies SHA-256 hashes against manifest entries.
