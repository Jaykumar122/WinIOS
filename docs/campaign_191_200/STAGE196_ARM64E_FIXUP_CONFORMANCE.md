# WINIOS STAGE 196: CHAINED FIXUPS AND ARM64e VALIDATION
## Conformance Audit Against Apple dyld fixup-chains.h & Pointer Authentication Mechanics

**Target:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120` / ARM64e)  
**Host Toolchain:** Windows 11 Pro x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Apple Reference Source:** `https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/fixup-chains.h`  
**Deliverable Document:** `docs/campaign_191_200/STAGE196_ARM64E_FIXUP_CONFORMANCE.md`

---

### 1. Pointer Format Conformance Matrix

WinIOS implements in-place memory unchaining for the modern Apple dyld chained fixups architecture (`LC_DYLD_CHAINED_FIXUPS`):

| Chained Format ID | Macro Name in `fixup-chains.h` | Target Architecture | Implementation in WinIOS | Conformance Status |
| :--- | :--- | :--- | :--- | :---: |
| **1** | `DYLD_CHAINED_PTR_ARM64E` | iOS / macOS ARM64e | Authenticated & non-auth unchaining | **IMPLEMENTED** |
| **2** | `DYLD_CHAINED_PTR_64` | macOS / iOS ARM64 & x86_64 | Standard 64-bit rebase/bind unchaining | **IMPLEMENTED** |
| **6** | `DYLD_CHAINED_PTR_64_OFFSET` | iOS ARM64 caches | 32-bit segment-relative offset | **IMPLEMENTED** |
| **9** | `DYLD_CHAINED_PTR_ARM64E_USERLAND` | iOS 14+ ARM64e Userspace | Auth & non-auth bind/rebase | **IMPLEMENTED** |
| **12** | `DYLD_CHAINED_PTR_ARM64E_USERLAND24` | iOS 15+ ARM64e 24-bit imports | Expanded import ordinal range | **IMPLEMENTED** |
| **3** | `DYLD_CHAINED_PTR_32` | 32-bit legacy Darwin | Out of scope for 64-bit iPhone 15 | **EXCLUDED** |

---

### 2. Pointer Structure & Bitfield Verification

#### A. `DYLD_CHAINED_PTR_64`
Conforms to `struct dyld_chained_ptr_64_bind` and `dyld_chained_ptr_64_rebase`:
- **Rebase Format:**
  - `target` (bits 35:0): Unslid 36-bit pointer value
  - `high8` (bits 43:36): High 8 bits of virtual pointer
  - `reserved` (bits 50:44)
  - `next` (bits 62:51): Offset to next chain entry in 4-byte strides
  - `bind` (bit 63): `0` (rebase)
- **Bind Format:**
  - `ordinal` (bits 23:0): Import symbol table index
  - `addend` (bits 31:24): Signed 8-bit addend
  - `reserved` (bits 50:32)
  - `next` (bits 62:51): Stride to next pointer
  - `bind` (bit 63): `1` (bind)

#### B. `DYLD_CHAINED_PTR_ARM64E`
Conforms to `struct dyld_chained_ptr_arm64e_auth_bind`, `dyld_chained_ptr_arm64e_auth_rebase`, and non-auth variants:
- **Authenticated Bind (`auth == 1`, `bind == 1`):**
  - `ordinal` (bits 15:0): 16-bit import ordinal
  - `diversity` (bits 47:32): 16-bit PAC discriminator value
  - `addrDiv` (bit 48): Address diversity enabled flag
  - `key` (bits 50:49): Key selector (`00` = IA, `01` = IB, `10` = DA, `11` = DB)
  - `next` (bits 61:51): Chain stride
  - `bind` (bit 62): `1`
  - `auth` (bit 63): `1`
- **Authenticated Rebase (`auth == 1`, `bind == 0`):**
  - `target` (bits 31:0): Segment offset
  - Diversity, key, and next stride match the auth bind format.

---

### 3. Pointer Authentication (PAC) Disclosure & Limitations

> [!WARNING]
> **PAC Limitation Disclosure:**
> In WinIOS, when unchaining authenticated pointers (`auth == 1`), the loader parses the key selection (`IA/IB/DA/DB`) and diversity fields, and resolves the raw target virtual address into guest memory.
> While the PAC bitfields are faithfully parsed according to `fixup-chains.h`, full cryptographic validation using Apple Silicon hardware PAC instructions (`PACIA`, `AUTIA`, `PACDA`, `AUTDA`) in hardware is emulated in software; x86_64 hosts lack native ARMv8.3-A PAC registers.

---

### 4. Robustness and Defensive Boundary Verification

The implementation in `src/loader/dyld_engine.cpp` enforces strict bounds and integrity checks:
1. **Header Validation:** Checks `fixups_version == 0` and verifies payload size $\ge$ `sizeof(DyldChainedFixupsHeaderRaw)`.
2. **Offset Verification:** Ensures `starts_offset`, `imports_offset`, and `symbols_offset` do not exceed linking payload boundaries.
3. **Ordinal Boundary:** Verifies `ordinal < hdr->imports_count`, preventing out-of-bounds import table reads.
4. **Loop / Cycle Detection:** Tracks visited chain locations via `std::set<std::uint64_t> visited_locations`. If an infinite cyclic pointer chain is encountered, it returns `DyldLinkStatus::InvalidChainedFixups` rather than hanging the emulator.
5. **Memory Bounds:** Verifies `chain_loc + 8 <= memory.size()`.

---

### 5. Conclusion

WinIOS supports the required 64-bit and ARM64e chained fixup formats conforming to Apple's open-source dyld specification. Chained fixups are traversed, unchained, and patched in guest memory with comprehensive cycle and boundary protection.
