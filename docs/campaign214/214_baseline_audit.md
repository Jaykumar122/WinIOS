# WinIOS Campaign 214: Baseline Audit & Repository Integrity

## 1. Commit and Branch Verification
- **Required Baseline Commit:** `b7e0d93`
- **Full SHA-1 Commit Hash:** `b7e0d93f8661fed8949449d3f6739b867e00a331`
- **Parent Commit:** `d065badb7fe4fb1bbc8e80939bfb842db8ee50a7` (Campaign 212 Closeout)
- **Active Branch:** `campaign-191r`
- **Working Tree State:** Clean, 0 uncommitted changes.

## 2. Protected Paths Audit
All four protected paths were audited before commencing Campaign 214 work:
- `D:\WinISO26\WinIOS_README.md`: Verified present, SHA-256 = `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` (Match)
- `D:\WinISO26\WinIOS\docs\progress.md`: Verified present, SHA-256 = `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` (Match)
- `D:\WinISO26\WinISO26_README.md`: Verified absent (Does not exist)
- `D:\WinISO26\Inferno-Research/`: Verified isolated and untouched.

## 3. Campaign 213 Artifact Manifest Audit
All 14 artifacts recorded in `WinIOS/artifacts/campaign-213/213_sha256_manifest.txt` were verified against their cryptographic SHA-256 hashes:
1. `artifacts/campaign-213/counter_app.macho`: `123e0073f91eef5987596dae4298c05bb3198775c94e6e84fe85586fb96dcbe7` (50,416 B) - **OK**
2. `artifacts/campaign-213/counter_app.s`: `a6e55d864a80b8b03df88c2fba5878dc9a58bda7e303b17979092b15260b13da` (3,663 B) - **OK**
3. `artifacts/campaign-213/guest_shell.macho`: `ddbdb7bde38309c541eb2dffe485c3685353626dd0b068bc859b7c7a1914c798` (50,480 B) - **OK**
4. `artifacts/campaign-213/guest_shell.s`: `045957a22f318a8d1f79d28cd7e76efa54a4570fd8dd3d37a8f2f5dd5f02ebb7` (4,283 B) - **OK**
5. `artifacts/campaign-213/notes_app.macho`: `eab951eddaa892a60e8e3cd13d1d67692df9fcad367adf71fe288fd8c24ce1ab` (50,400 B) - **OK**
6. `artifacts/campaign-213/notes_app.s`: `73da2cc4b1dc9d04cbf5249af51e2ed267c34e543fa4fee22bb8e8189ce3e2c3` (4,300 B) - **OK**
7. `artifacts/campaign-213/screenshot_01_shell_homescreen.bmp`: `86ce3bd84bdbb27412615eb9a6e2e85fc1f75700c9d653bba8f9fed636d7d96b` (3,840,054 B) - **OK**
8. `artifacts/campaign-213/screenshot_02_counter_app_launched.bmp`: `89f9bd9e0fe0dffa6eadc934c68adb9ecb918c788845073d122623a202a56e7a` (3,840,054 B) - **OK**
9. `artifacts/campaign-213/screenshot_03_counter_app_incremented.bmp`: `89f9bd9e0fe0dffa6eadc934c68adb9ecb918c788845073d122623a202a56e7a` (3,840,054 B) - **OK**
10. `artifacts/campaign-213/screenshot_04_return_to_shell.bmp`: `89f9bd9e0fe0dffa6eadc934c68adb9ecb918c788845073d122623a202a56e7a` (3,840,054 B) - **OK**
11. `artifacts/campaign-213/screenshot_05_notes_app_launched.bmp`: `bc3d697ae85869a0aa62d5e713500c300bb128df59b2ea9f8ddbe5e4edb892cf` (3,840,054 B) - **OK**
12. `artifacts/campaign-213/screenshot_06_notes_app_checked.bmp`: `1610a8668d538f82575435b092f1b40e16701fc6b5a1b043677712d300643eaf` (3,840,054 B) - **OK**
13. `artifacts/campaign-213/screenshot_07_shell_closeout.bmp`: `1610a8668d538f82575435b092f1b40e16701fc6b5a1b043677712d300643eaf` (3,840,054 B) - **OK**
14. `artifacts/campaign-213/test_host_presentation.bmp`: `72d2db9a1839935c6828fa9a75edf17fc219b563d21251f8ec6ab43fac8b04df` (320,054 B) - **OK**

## 4. Test Suite Baseline Execution
All 20 Campaign 213 test suites were executed under MSVC Debug CTest:
- Total Tests: 20
- Passed: 20 (100%)
- Failed: 0
- Execution Time: 1.07 seconds
- Regressions on Campaign 211 & 212 tests: 0 (33/33 passed)

Stage 214.0 Baseline Audit completed with 100% integrity.
