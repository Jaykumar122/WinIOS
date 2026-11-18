# WinIOS Campaign 224 — Stage 224.12 Storage Recovery & Consistency Guarantees

## 1. Atomic Commit Protocol
File mutations in `IsolatedStorage` follow a 4-phase atomic commit sequence:

1. **Transaction Staging:** Content is written to `/data/<app_id>/<path>.tmp.<tx_counter>`.
2. **Cryptographic Validation:** If `verify_sha256` is enabled, the written temp file is read back and verified against the computed SHA-256 digest. Corrupted data triggers immediate deletion and returns `StorageResult::CorruptedData`.
3. **Commit Phase:** The target file is replaced atomically.
4. **Cleanup Phase:** The temporary staging file is unlinked.

---

## 2. Crash Recovery Protocol
If an application or system process terminates unexpectedly during file write:
- The target file remains intact at its previous consistent version.
- On startup or explicit maintenance invocation, `recover_transactions()` recursively traverses all nested subdirectories under `/data/<app_id>/`.
- Any uncommitted `.tmp.*` files are unlinked, restoring clean storage state without data leakage or quota penalties.
