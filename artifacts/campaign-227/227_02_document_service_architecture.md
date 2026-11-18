# Campaign 227 — Stage 227.2: Document Service Architecture

## 1. System Overview
Campaign 227 establishes a unified, persistent, document-oriented application service layer for WinIOS. The document system bridges the virtual filesystem (VFS), capability security manager, window manager, application registry, and desktop shell into a cohesive user-facing document platform.

```
+---------------------------------------------------------------------------------+
|                                 Desktop Shell                                   |
|   (DesktopController, WindowManager, FilePicker UI, Recent Documents Menu)      |
+---------------------------------------------------------------------------------+
                                       |
+---------------------------------------------------------------------------------+
|                             Document Service                                    |
|   +--------------------------+  +--------------------------+                    |
|   | DocumentRegistry         |  | DocumentStreamManager    |                    |
|   | - Metadata tracking      |  | - Read/Write by offset   |                    |
|   | - 64-bit gen handles     |  | - Bounded buffering      |                    |
|   +--------------------------+  +--------------------------+                    |
|   +--------------------------+  +--------------------------+                    |
|   | DocumentAccessGrants     |  | AtomicSaveManager        |                    |
|   | - Process-scoped grants  |  | - Temporary staging      |                    |
|   | - Revocation & audit     |  | - Atomic promotion       |                    |
|   +--------------------------+  +--------------------------+                    |
+---------------------------------------------------------------------------------+
                                       |
+---------------------------------------------------------------------------------+
|                       Platform Infrastructure Services                          |
|   CapabilityManager | VirtualFileSystem (VFS) | ApplicationRegistry | Sessions  |
+---------------------------------------------------------------------------------+
```

## 2. Document Identity & Generation Handle Model
To prevent forged handle attacks and ABA recycling bugs, documents are identified via 64-bit generation-tagged handles:
$$\text{Handle} = (\text{generation} \ll 32) \mid \text{doc\_id}$$
- `doc_id`: 32-bit slot index allocated monotonically by `DocumentRegistry`.
- `generation`: 32-bit generation counter incremented whenever a document slot is closed or deleted.
- Non-zero generation wraparound protection guarantees `generation` never resets to 0.

## 3. Metadata Specification
Every registered document maintains a rich descriptor:
- `doc_handle`: 64-bit generation handle.
- `owner_bundle_id`: String bundle ID of the owning application (e.g. `"com.winios.editor"`).
- `owner_pid`: Monotonic process ID of the creating process.
- `display_name`: Human-readable document name (e.g. `"Report.txt"`, max 64 characters).
- `canonical_vfs_path`: Fully qualified virtual path (e.g. `"/Documents/Report.txt"`, max 256 characters).
- `mime_type`: Normalized MIME string (e.g. `"text/plain"`).
- `file_size_bytes`: Current file size (strictly bounded, max 10 MB).
- `created_time_us`: Microsecond timestamp of creation.
- `modified_time_us`: Microsecond timestamp of last modification.
- `is_read_only`: Boolean flag indicating write prohibition.

## 4. Capability Security & Ownership Boundaries
Document operations require explicit permissions enforced at every entry point:
- `Capability::DocumentRead` (`1 << 21`): Required to open/read documents.
- `Capability::DocumentWrite` (`1 << 22`): Required to edit/save documents.
- `Capability::DocumentCreate` (`1 << 23`): Required to create new documents.
- `Capability::DocumentDelete` (`1 << 24`): Required to delete documents.
- `Capability::DocumentShare` (`1 << 25`): Required to grant or revoke document access.
- `Capability::DocumentEnumerate` (`1 << 26`): Required to list document registries and recent histories.

Processes may only access documents that:
1. Are owned by the process's own `bundle_id` or `pid`.
2. OR have an active, unrevoked `DocumentAccessGrant` issued specifically to the requesting PID.

## 5. Bounded Document Streams
Document I/O operates through bounded stream descriptors:
- Maximum document size: **10 MB** (`10,485,760 bytes`).
- Stream modes: `Read` (`0x1`), `Write` (`0x2`), `ReadWrite` (`0x3`).
- Operations: `read(offset, len)`, `write(offset, data, len)`, `seek(offset)`, `flush()`, `close()`.
- Overflow protection: Integer bounds checks prevent wrap-around when computing `offset + len`.

## 6. Atomic Save & Crash Recovery
To prevent data loss from application crashes or power interruptions during write operations:
1. **Staged Write**: Data is written to a temporary sibling file (e.g. `path + ".tmp"`).
2. **Checksum Validation**: SHA-256 or FNV-1a checksum is validated against written bytes.
3. **Atomic Rename / Promotion**: The temporary file is promoted to the destination path.
4. **Journal Cleanup**: The staging metadata is committed to disk.

## 7. Persistent Desktop Sessions
The session manager records:
- Open document references and their associated windows.
- Window geometry (x, y, width, height) and z-stack order.
- Active focused window.
- Persisted to `"/System/Library/Sessions/desktop_session.json"`.
- Validated on desktop startup: missing applications or inaccessible files are pruned gracefully without stalling the compositor.
