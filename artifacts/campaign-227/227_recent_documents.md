# WinIOS Campaign 227: Recent Documents Manager Architecture

## 1. Overview & Purpose

The Recent Documents Manager maintains bounded, per-application histories of opened and saved documents across desktop sessions. This service enables applications (such as Text Editor and Document Viewer) to display recent files menus, jump lists, and restore working documents upon relaunch.

---

## 2. Data Structure & Bounds

The manager tracks entries via `RecentDocumentEntry`:

```cpp
struct RecentDocumentEntry {
    DocumentHandle  doc_handle{kInvalidDocumentHandle};
    std::string     bundle_id;
    std::string     display_name;
    std::string     canonical_path;
    std::uint64_t   last_opened_time_us{0};
};
```

### 2.1 Bounded List Invariant
- **Max Items Per Application:** Strictly capped at `16` entries.
- **LRU Eviction:** Adding an entry when the list contains 16 items causes the least recently opened item (oldest `last_opened_time_us`) to be evicted.
- **Move-to-Front:** Opening an already registered path updates its timestamp and moves it to the front of the list.

---

## 3. Pruning & Stale Document Cleanup

Before returning recent entries to applications, the manager executes `prune_missing()`:
1. Each `canonical_path` is verified against the `VirtualFileSystem`.
2. If the backing file was deleted or moved, its entry is automatically purged from the list.
3. This prevents dead links from appearing in guest application UI menus.

---

## 4. Syscall 580: Query Contract

Applications access their recent history via `SYS_WINIOS_DOC_GET_RECENT (580)`:
- Register `0`: Destination user pointer for `winios_recent_doc_t` array.
- Register `1`: Maximum count of items to copy.
- Register `2`: Pointer to receive the actual count copied.
- Memory Layout: Each entry is structured as a 208-byte record containing 64-bit handle, display name (64 chars), canonical path (128 chars), and 64-bit microsecond timestamp.
