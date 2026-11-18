# WinIOS Campaign 227: Document Identity, Registry & Access Control Architecture

## 1. Overview & Architectural Role

Campaign 227 introduces the WinIOS Document Registry, establishing a secure, capability-checked, generation-counted handle system for persistent document references across isolated ARM64 guest applications.

Prior to Campaign 227, applications could access virtual filesystem nodes directly if permitted by general sandbox policies. The Document Registry introduces a mediated, identity-centric abstraction where:
1. Files are represented to guest applications as opaque, 64-bit generation handles (`DocumentHandle`).
2. Guest processes cannot access or manipulate documents without explicit capability grants (`DocumentRead`, `DocumentWrite`, `DocumentCreate`, `DocumentDelete`, `DocumentShare`).
3. Cross-process document sharing is managed through cryptographically tracked access grant tokens, allowing document owners to delegate constrained permissions to peer applications without exposing broad filesystem access.

---

## 2. 64-Bit Generation Handle Model

To prevent stale handle reuse, handle forgery, and use-after-free vulnerabilities in user-space guest workloads, `DocumentHandle` encodes both a 32-bit monotonic identifier and a 32-bit generation counter:

```
+------------------------------------+------------------------------------+
|       32-Bit Generation Counter    |        32-Bit Monotonic ID         |
|             (Bits 63..32)          |            (Bits 31..0)            |
+------------------------------------+------------------------------------+
```

- **ID Allocation:** Starts at `1` and increments monotonically upon each document registration.
- **Generation Tracking:** Starts at `1`. When a document is unregistered or removed, its generation counter is incremented. Any subsequent syscall attempting to access the resource with the prior generation is rejected with `NotFound` or `StaleHandle`.
- **Validation Guard:** Bit 0 of ID cannot be 0 for valid handles; ID 0 and Generation 0 are reserved as `kInvalidDocumentHandle = 0`.

---

## 3. Path Sanitization & Permitted Sandbox Roots

The Document Registry strictly enforces canonical root boundaries to prevent directory traversal and host escape attacks:

```cpp
bool DocumentRegistry::sanitize_and_validate_path(const std::string& in_path, std::string& out_canonical)
```

1. **Traversal Denial:** Sequences such as `..`, `./`, and backslashes are rejected immediately.
2. **Permitted Roots:** Documents can only be registered within approved virtual partitions:
   - `/Documents` — User personal document storage.
   - `/Shared` — Platform inter-application shared document exchange.
   - `/Downloads` — Ingress network or imported files.
   - `/tmp` — Ephemeral temporary scratch documents.
3. **Canonical Normalization:** Multiple slashes (`//`) are normalized to single delimiters, and trailing slashes are pruned.

---

## 4. Capability Enforcement Matrix

Every registry operation validates caller PID capabilities via `CapabilityManager`:

| Operation | Required Capability | Notes |
|---|---|---|
| `register_document` | `DocumentCreate` | Caller becomes owner; initial metadata registered |
| `unregister_document` | `DocumentDelete` | Caller must be document owner or PID 1 (System) |
| `get_metadata` | `DocumentRead` | Caller must be owner, grantee, or PID 1 |
| `check_access(Read)` | `DocumentRead` | Validates direct ownership or active unexpired grant |
| `check_access(Write)`| `DocumentWrite`| Validates direct ownership or active unexpired grant |
| `create_grant` | `DocumentShare` | Caller must be document owner |
| `revoke_grant` | `DocumentShare` | Caller must be original grantor or PID 1 |

---

## 5. Cross-Process Access Grant Lifecycle

Document owners can delegate temporary or indefinite read/write permissions to peer processes:
1. **Grant Creation:** Owner calls `SYS_WINIOS_DOC_GRANT_ACCESS (579)` specifying target `grantee_pid` and access mode.
2. **Grant Token:** Registry assigns a unique 64-bit token (`grant_token`) starting at `5001`.
3. **Grant Validation:** When the peer requests a stream, the registry verifies:
   - The document handle is valid and generation matches.
   - The grantee PID matches the calling process.
   - The grant is not marked `is_revoked`.
   - The expiry timestamp (`expiry_time_us`), if nonzero, has not passed.
4. **Revocation:** Owners can revoke specific grants via token or revoke all issued grants upon process termination.
