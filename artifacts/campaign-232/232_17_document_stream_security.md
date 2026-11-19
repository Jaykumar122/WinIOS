# Stage 232.17 — Document Grants & File Stream Security Specification & Evidence

**Campaign:** 232  
**Stage:** 232.17  
**Component:** `winios::documents::DocumentRegistry`, `winios::documents::DocumentStreamManager`, `winios::documents::DocumentAccessGrant`  
**Files Modified:**
- `src/documents/document_stream_manager.h`
- `src/documents/document_stream_manager.cpp`
- `tests/unit/stage232_17_document_security_tests.cpp`  
**Test Executable:** `build/RelWithDebInfo/stage232_17_document_security_tests.exe`  
**CTest Target:** Test #935 (`stage232_17_document_security_tests`)

---

## 1. Security Architecture & Threat Model

Document isolation and cross-process sharing must maintain strict confidentiality and integrity across process lifecycles:

1. **Immediate Revocation Propagation:**
   When an owner process revokes an existing `DocumentAccessGrant`, any active stream handle previously opened by the grantee must immediately lose read and write capabilities. In previous implementations, stream read and write operations only validated the stream owner PID, which allowed an adversary to retain stream access after a grant was revoked.
2. **Time-Bounded Access Windows:**
   Grants specify an expiration time (`expiry_time_us`). Access must be automatically denied once the window elapses without needing manual revocation.
3. **Generational PID Reuse Defense:**
   Grants record the grantee's process generation (`grantee_generation`). If a grantee process exits and its PID is subsequently recycled for another process, the recycled process must not inherit the grant.
4. **Stream Confinement & Bounds Checking:**
   Stream reads must be strictly bounded to the available bytes in the stream buffer. File sizes must never exceed the 10 MB system document limit (`kMaxDocumentSizeBytes`).

---

## 2. Hardening Implementation

### 2.1 Dynamic Stream Access Re-Verification
In `DocumentStreamManager::read_stream`, `write_stream`, and `flush_stream`:
```cpp
// Dynamic verification: grant may have been revoked or expired since stream opened
if (registry_ && !registry_->check_access(it->second.doc_handle, caller_pid, it->second.mode)) {
    return false;
}
```
Every read, write, and flush operation dynamically consults `DocumentRegistry::check_access`. If the grant was revoked or expired, the operation is blocked.

### 2.2 Dedicated Document & Grantee Stream Invalidation
Added `close_streams_for_document(doc_handle)` and `close_streams_for_grantee(doc_handle, grantee_pid)` in `DocumentStreamManager` to purge stream instances from memory upon document deregistration or selective revocation.

### 2.3 Generational Validation in `DocumentRegistry::check_access`
`check_access` cross-references `grant.grantee_generation` against `identity_mgr_->get_generation(caller_pid)`. Stale recycled handles are rejected immediately.

---

## 3. Verification Log Output

```text
========================================================
STAGE 232.17: Document Grants and File Stream Security Tests
========================================================
[Stage 232.17] Testing grant revocation invalidation of active streams...
[Stage 232.17] Testing time-bounded document grant expiration...
[Stage 232.17] Testing generational process handle stale grant rejection...
[Stage 232.17] Testing stream read/write offset and size boundary confinement...
All Stage 232.17 tests passed successfully!
```

CTest Result:
```text
      Start 935: stage232_17_document_security_tests
11/11 Test #935: stage232_17_document_security_tests .........   Passed    0.03 sec
```
Status: **PASSED (100%)**
