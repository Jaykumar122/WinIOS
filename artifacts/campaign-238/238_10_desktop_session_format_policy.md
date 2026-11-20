# Stage 238.10: Desktop Session Format & Recovery Policy

## 1. Problem Statement & Audit
In Campaign 237, `DesktopSessionManager::deserialize` introduced an ad-hoc string check to reject invalid input:
```cpp
if (data.find("gibberish") != std::string::npos) {
    return false;
}
```
This heuristic had severe deficiencies:
1. It relied on a hardcoded keyword rather than structural syntax validation.
2. Any real corrupted, truncated, or binary data lacking the word `"gibberish"` slipped past the check.
3. It conflated recoverable legacy sessions with fatal malformed payloads.

## 2. Structured Recovery Architecture
The parser was redesigned with principled format detection and recovery rules:
1. **Strict Rejection of Non-Text and JSON**:
   - Inputs containing binary null bytes (`\0`) are immediately rejected as invalid binary corruption.
   - Inputs starting with `{` or `[` are detected as JSON payloads and rejected (the session store uses INI format).
2. **Unsupported Future Versions**:
   - If `version=N` where `N > 2` is encountered, the parser strictly fails with `false` to prevent corrupted state interpretation from incompatible schemas.
3. **Graceful Degraded INI Recovery**:
   - If an input contains valid INI structural lines (e.g. `[desktop]`, `[window_...]`, `key=val`) but lacks a version header, it is classified as a degraded legacy session.
   - The parser logs a diagnostic warning, resets internal state cleanly, and recovers successfully with an empty session rather than crashing or throwing unhandled errors.
4. **Empty / Whitespace Input**:
   - Empty or whitespace-only data returns `false` (no valid session structure).

## 3. Implementation
Modified `src/documents/desktop_session_manager.cpp`:
- Removed all checks for magic strings (`"gibberish"`).
- Implemented `has_null_bytes`, `is_json_like`, `has_valid_ini_structure`, and version boundary enforcement.
