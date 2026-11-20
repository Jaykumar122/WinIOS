# Stage 243.3: Replay State Coverage Specification

**Campaign**: 243
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Formulated an explicit replay coverage and exclusion matrix:

| State Domain | Included / Covered | Comparison Granularity | Exclusion Justification |
|--------------|-------------------|------------------------|--------------------------|
| GPRs X0-X30  | YES               | Exact 64-bit value     | N/A |
| PC, SP, NZCV | YES               | Exact architectural    | N/A |
| Memory Hash  | YES               | FNV-1a + 256-bit digest| N/A |
| Page Hashes  | YES               | Sparse per-16KB page   | N/A |
| Syscall Log  | YES               | Number, args, ret, CF  | N/A |
| Wall-Clock   | EXCLUDED          | N/A                    | Host nondeterminism |
| Random Seed  | EXCLUDED          | N/A                    | Host external entropy |
| Host Threads | EXCLUDED          | N/A                    | Preemption scheduling |
