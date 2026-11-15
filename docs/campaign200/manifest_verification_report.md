# WinIOS Campaign 200: Manifest Verification Report

**Campaign:** 200
**Target Profile:** iPhone 15 (iPhone15,4 / D37AP / Apple A16 Bionic T8120)
**Host Environment:** Windows 11 x64 (Visual Studio 2026, MSVC C++20, /W4 /WX /permissive-)
**Secondary Toolchain:** Zig 0.17.0 (-target aarch64-macos -mcpu=apple_a16)
**Total Manifest Entries:** 36 verified artifacts

---

## Cryptographic SHA-256 Digest Ledger

| Relative File Path | Size (Bytes) | SHA-256 Digest |
| :--- | :--- | :--- |
| `docs/campaign200/stage200_0_baseline_audit.md` | 5523 | `b066ed60c4ace9b69eebda0d4009c40856944e716c01206cdd8278e680a03b20` |
| `docs/campaign200/architecture_dependency_audit.md` | 5398 | `16e231e5eecfef1c01c0a6adddf32f46111e4166bcb4efc6585eea09a0ba72e9` |
| `docs/campaign200/integration_contracts.md` | 4684 | `efcf03980b2532e4f872f01ac098913f807335a3f0578bf3381b681f7a92c2b0` |
| `docs/campaign200/dma_end_to_end_validation.md` | 4060 | `b4e6a0334452fa34f2873cb18cb11738b276c522e445bccd91eee1d559146e6b` |
| `docs/campaign200/platform_handoff_validation.md` | 3243 | `a7e3f1e2b55a2b8aad228b3edd6c10bcb2cf8ff5e98f1f616d55465450461684` |
| `docs/campaign200/snapshot_replay_validation.md` | 3581 | `0bc7138f4e398fd98de73b87e1b5c19155aee91617e86b483bbcd7b836c57024` |
| `docs/campaign200/end_to_end_guest_workload.md` | 3104 | `9e7c10aacaa0e0ff4207ebefcbb8119d93285759f6dfdc45fa818b026ddcdde0` |
| `docs/campaign200/architecture_conformance_report.md` | 9404 | `e736c5453e0535b4a0f467702ced534e8746214b28b5ba5ef017edcf6c30ff94` |
| `docs/campaign200/known_limitations.md` | 4324 | `4d2c59e681a53d885280406ee238aa48af3bc0cfa84aaa97cd79ebc74ad8a55a` |
| `docs/campaign200/evidence_matrix.md` | 5853 | `6bd5ab4b73233b03be1370aa14a51c3e5c1151a6b239aa1b5c37163b556fea3e` |
| `docs/campaign200/campaign200_final_report.md` | 10064 | `54aee8aa02919f73d053d44b0d59b96e5dd7995ff7161b64dd4b2b619f7a3836` |
| `docs/campaign200/test_results_release.txt` | 67786 | `ba73766c8a62a234272332e3738836fc18ec7a8a0b490c3b0d4f4dfc0d9d6448` |
| `docs/campaign200/test_results_debug.txt` | 67786 | `3d8269bb681cbc303b56974411bd86ec5878e8b880de1fcd11c4460ca68b3c26` |
| `tests/artifacts/src/guest_stage200_end_to_end_workload.S` | 3055 | `bc6826f66decc74817a3d3dd6883ac7fe4223d05a86b1599b5c4ecd676d95a65` |
| `tests/artifacts/bin/guest_stage200_end_to_end_workload` | 50176 | `998b268c20caee67dfe96550fe7ce1f023c97967a6ca5b0585b98f0d0b6e8933` |
| `tests/unit/stage200_3_stage1_translation_tests.cpp` | 7556 | `c8735b9504a9a33138b1f85e3de6b1f564fd0f20267ded73ea8896935e3a1456` |
| `tests/unit/stage200_4_stage2_translation_tests.cpp` | 6790 | `38de2018fc0368c05e894dbbbb283147f47da8758118aa193e552feeaa739336` |
| `tests/unit/stage200_5_combined_fault_propagation_tests.cpp` | 11293 | `b99a0e216d163534eeaec447f0f15854d2f41411eb334ddcad3161f1e87683a9` |
| `tests/unit/stage200_6_tlb_concurrency_tests.cpp` | 9882 | `2fd13852d852e1972518b51878809520c6d9101d06fc948bcc8f17970b873bab` |
| `tests/unit/stage200_7_memory_cache_integration_tests.cpp` | 6299 | `4fc4a7203b5f4400b61314db7356cd520a9806e8eb7291fd0cf24af7ea8a0390` |
| `tests/unit/stage200_8_iommu_lifecycle_tests.cpp` | 5735 | `865548532ea92e8b114e570749e428f9c12a2b64e9cf81d0fc85c17003cb7e02` |
| `tests/unit/stage200_9_iova_fault_tests.cpp` | 5717 | `c2ef8f0efb8497ac5bc43d800be12d7301ad0ed85129700a25a1afe32d0550c7` |
| `tests/unit/stage200_10_dma_coherency_tests.cpp` | 5728 | `a49ef5f1eaedc90b8846bee17a7bc304f8a6e7b416a09f3cf7d32f48a2a344b0` |
| `tests/unit/stage200_11_dma_interrupt_tests.cpp` | 5191 | `71dba3e8bd3c5a5f368e92d94c5e91584d0218ef01f8a0af93d1e673347be188` |
| `tests/unit/stage200_12_pac_conformance_tests.cpp` | 8477 | `540732fc164bec9096d2263b5187dd9e5dbcd310af462a2e6e3314581337b9ce` |
| `tests/unit/stage200_13_pac_isolation_tests.cpp` | 6295 | `087d6601ec731171597adac86561e8c867eee45abf003a315dc747aef41d6b31` |
| `tests/unit/stage200_14_pac_exception_tests.cpp` | 5700 | `471c5967efa9011131c0a6db928ec9aa7e9a76f0f9c1242c30b01418dee54c05` |
| `tests/unit/stage200_15_exception_level_tests.cpp` | 5641 | `85fad168e5b34e6f9428788f1345159a5ab8668298aa7f965838ba4e3e7b4d7f` |
| `tests/unit/stage200_16_gicv3_conformance_tests.cpp` | 5490 | `c41530edd60232cd65c9eab30c660a42941e8071b60b24d8928baa156cd6b9b6` |
| `tests/unit/stage200_17_multicore_interrupt_race_tests.cpp` | 5155 | `0796aa835e234671b2eb7aa68a448e54088433cc9e7173152e4e4140736320c7` |
| `tests/unit/stage200_18_devicetree_consistency_tests.cpp` | 4206 | `399608f326658763e4f41e933c2ab2234025c503d1552aae6b48dd0141991ed7` |
| `tests/unit/stage200_19_boot_handoff_tests.cpp` | 3272 | `e22828de579eee2fb83eadcf76bee6a1be468fe1fa6ccb63abef82ddc81837d1` |
| `tests/unit/stage200_20_complete_snapshot_tests.cpp` | 3184 | `7a5dc885a1e8e4b012b3edac6fe88def5b9e5e576e0b13fd7db9efc35b34d216` |
| `tests/unit/stage200_21_snapshot_recovery_tests.cpp` | 5226 | `9311c0a3eacf970630cfae753bcd6334895dfada11bafbf713dc09847e423d38` |
| `tests/unit/stage200_22_deterministic_replay_tests.cpp` | 6273 | `c2c27a8821851dfe16eff3879deb6b55dca746f5667d63c21a0822e3a2e0f1ec` |
| `tests/unit/stage200_23_guest_workload_tests.cpp` | 6838 | `e1059108ce2a1455e6b7fd470e4e67dc45e0e5a016c934e5e58e6ef8d4681fb1` |

---

## Verification Verdict

All 36 Campaign 200 artifact files, test suites, compiled Mach-O binaries, and architecture reports have been cryptographically verified with SHA-256 digests.
