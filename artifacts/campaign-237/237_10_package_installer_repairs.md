# Campaign 237 — Stage 237.10: Package Installer & Canonical Path Repairs

## 1. Executive Summary

Stage 237.10 investigated and resolved failure #908 (`stage231_6_canonical_path_tests`), which verifies canonical path validation and containment across package installation directories and virtual filesystem trees.

---

## 2. Root Cause & Technical Repair

### 2.1 Parent Reparse Point Walk Termination Flaw (#908)
- **Root Cause**:
  In `CanonicalPathValidator::is_strictly_inside_root(root_dir, target_path)`, when `target_path` did not yet exist on disk, the validator walked up the parent directory chain to detect whether any intermediate directory was a junction or reparse point escaping `root_dir`.
  However, the `while (!parent.empty() && parent != cur)` loop failed to terminate upon reaching `root_dir` itself. Consequently, it continued walking all the way up to volume roots (e.g. `D:\`), which obviously did not have `root_dir` (`D:\WinIOS\apps\com.sample.app`) as a prefix, causing the validator to erroneously flag `D:\` as an "escaping junction" and return `false`.

- **Correction**:
  In `src/packaging/canonical_path_validator.cpp`, added termination logic:
  ```cpp
  std::filesystem::path cur = std::filesystem::path(target_path).lexically_normal();
  std::filesystem::path norm_root_fs = std::filesystem::path(root_dir).lexically_normal();
  auto parent = cur.parent_path();
  while (!parent.empty() && parent != cur) {
      std::string p_str = parent.generic_string();
      std::string r_str = norm_root_fs.generic_string();
      if (p_str.size() <= r_str.size()) {
          break; // Reached root directory or ancestor; stop checking children
      }
      ...
  ```
  Once the walk reaches the root boundary or higher, the loop stops. Child directories inside `root_dir` continue to be inspected for junction escapes, while the root boundary itself and its ancestors are not falsely flagged.

---

## 3. Verification

- `stage231_6_canonical_path_tests` passed 100% across all path normalization, directory traversal, Windows device name, Alternate Data Stream, UNC, and `is_strictly_inside_root` test vectors in both `RelWithDebInfo` and `Debug`.
