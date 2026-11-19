# Stage 232.9 Deliverable — Package Canonicalization & Signature Domain

## 1. Overview
Stage 232.9 hardens `AppPackage::pack_v3` to enforce 100% deterministic, bit-for-bit reproducible packaging and ensures the cryptographic signature domain covers the entire canonical byte stream (`src/packaging/app_package.cpp`).

## 2. Hardened Canonicalization Invariants
1. **Entry Path Normalization:**
   - All Windows backslashes (`\`) are normalized to standard forward slashes (`/`).
   - Redundant leading `./` and `/` characters are stripped.
2. **Lexicographical Entry Ordering:**
   - Manifest file entries are sorted strictly in lexicographical order by normalized relative path before directory table construction.
   - Manifest permissions, optional permissions, and supported document types are sorted lexicographically before serialization.
3. **Deterministic Alignment & Padding:**
   - File payload boundaries are padded to 16-byte alignment using zero bytes (`0x00`).
   - Header reserved fields are explicitly zeroed.
4. **Signature Domain Canonical Coverage:**
   - The WIOSAUTH v2 trailer content digest computes the SHA-256 over the entire pre-trailer byte stream $[0 \dots \text{trailer\_start}]$, covering header, manifest string, directory table, zero-padding, and all file payloads.
   - Modifying a single alignment padding byte immediately invalidates the package's content digest and digital signature.

## 3. Test Verification
- Executed unit tests in `tests/unit/stage232_9_canonical_package_tests.cpp` (registered as CTest #930).
- Tests passed:
  1. `test_deterministic_byte_stream_reproducibility`: Independent package instances with identical contents produce 100% bit-for-bit identical outputs.
  2. `test_entry_order_independence`: Adding files and permissions in reverse order produces identical canonical packages.
  3. `test_path_separator_normalization`: Mixed Windows backslashes and relative paths normalize to identical outputs.
  4. `test_signature_domain_canonical_coverage`: Mutating alignment padding bytes causes signature failure.
  5. `test_unpack_roundtrip`: Unpacking canonical v3 packages restores files, manifests, and hashes cleanly.
- Build Status: Zero warnings and zero errors under MSVC `/W4 /WX /permissive-` in both Debug and RelWithDebInfo.
