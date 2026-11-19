# Stage 231.16 — Reproducible Guest Application Builds

## Overview
Stage 231.16 establishes end-to-end build reproducibility for freestanding ARM64 guest applications and package artifacts. It eliminates non-deterministic serialization, prevents ordering divergence in file directory tables, and guarantees byte-for-byte identical package generation and SHA-256 digests across repeated builds and shuffled inputs.

## Key Reproducibility Controls
1. **Canonical File Sorting**:
   - `AppPackage::pack_v3()` now lexicographically sorts all file entries by canonical relative path prior to writing the package manifest and directory table.
   - Verified that adding files in reversed or permuted order produces an identical byte sequence and identical SHA-256 digest.
2. **Deterministic Code & Build Script Generation**:
   - `AppGenerator::generate_c_source`, `generate_manifest_ini`, and `generate_build_script` emit normalized, deterministic templates without host-specific timestamps or variable metadata.
   - Pinned compilation flags for Zig 0.17.0 ARM64 freestanding target: `-target aarch64-freestanding -ffreestanding -O2`.
3. **Repeat Build Stability**:
   - Executed 10 sequential package generation and serialization iterations.
   - Confirmed 100% digest stability across all iterations.
4. **Deterministic Digital Signing**:
   - Package signing via `PublisherTrustStore::sign_package` with fixed key material and normalized timestamps produces bit-for-bit identical signed containers and verification results.

## Verification
- **Test Suite**: `tests/unit/stage231_16_reproducible_build_tests.cpp` (CTest #918).
- **Test Results**:
  - RelWithDebInfo: Test #918 Passed in 0.07s.
  - Debug: Test #918 Passed in 0.12s.
