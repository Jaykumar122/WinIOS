# Campaign 222 — Stage 222.0 Baseline Verification Report

## Environment & Metadata
- **Campaign**: 222 — Interactive Native ARM64 Application Environment
- **Stage**: 222.0 — Baseline Recording & Repository Integrity
- **Repository Root**: `D:\WinISO26\WinIOS\`
- **Branch**: `campaign-191r`
- **Baseline Commit**: `bb7d4d18bc821511238475ca5b0c5adf35a3ba0b`
- **Host Platform**: Windows 11 x64 (MSVC C++20 `/W4 /WX /permissive-`, CMake 3.31.6, CTest)
- **Guest Target**: Apple iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), ARM64, 16 KB translation granule
- **Pinned Public XNU Reference**: `xnu-10002.1.13`
- **Cross-Compiler**: Zig 0.17.0 (`zig cc -target aarch64-macos -nostdlib -ffreestanding -fno-builtin -fno-sanitize=all -O2 "-Wl,-e,_start"`)

## Repository Integrity Audit
1. **Working Tree**: Clean (`git status` reports nothing to commit, working tree clean).
2. **Current Branch**: `campaign-191r`.
3. **HEAD Commit Hash**: `bb7d4d18bc821511238475ca5b0c5adf35a3ba0b`.

## Protected Files Verification
| Protected Path | Expected Status / SHA-256 | Actual Status / SHA-256 | Conformance |
|---|---|---|---|
| `D:\WinISO26\WinISO26_README.md` | Nonexistent (`False`) | Nonexistent (`False`) | PASS |
| `D:\WinISO26\WinIOS_README.md` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | PASS |
| `D:\WinISO26\WinIOS\docs\progress.md` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | PASS |
| `D:\WinISO26\Inferno-Research\` | Untouched | Directory intact, zero modifications | PASS |

## Baseline Test Suite Conformance
- Test execution across 10 Campaign 221 test suites (`stage221_2` to `stage221_23`): 10 passed, 0 failed (100% pass rate).
- Baseline verification is complete and confirmed intact.
