# Stage 243.4: Memory Hash and Page Coverage Correctness

**Campaign**: 243
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Audited memory digest mechanics and coverage:

- **Dual Digest**: Employs fast FNV-1a 64-bit hash combined with 256-bit multi-lane software digest (`compute_digest256`).
- **Page Coverage**: Only valid, mapped 16KB pages in `UserAddressSpace` are hashed.
- **Perturbation Sensitivity**: Tested and proven that flipping a single bit in any mapped page alters both digests and triggers divergence.
