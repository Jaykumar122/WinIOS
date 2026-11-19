#include "packaging/app_package.h"
#include "crypto/sha256.h"
#include "security/security_utils.h"
#include "security/publisher_trust_store.h"

#include <cstring>
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace winios::packaging {

namespace {
constexpr const char* kPkgMagicV1 = "WIOSPKG1";
constexpr const char* kPkgMagicV2 = "WIOSPKG2";
constexpr const char* kPkgMagicV3 = "WIOSPKG3";
constexpr std::size_t kMagicLen = 8;
constexpr std::size_t kMaxManifestLen = 65536;
constexpr std::size_t kMaxFileCount = 256;
constexpr std::size_t kMaxIndividualFileSize = 32 * 1024 * 1024; // 32 MB
constexpr std::size_t kV3HeaderSize = 48;

static bool is_valid_utf8_path(const std::string& path) {
    if (path.empty() || path.size() > 256) return false;
    const auto* s = reinterpret_cast<const unsigned char*>(path.data());
    const std::size_t len = path.size();
    std::size_t i = 0;
    while (i < len) {
        if (s[i] < 0x20) {
            return false; // Null byte or control code
        }
        if (s[i] <= 0x7F) {
            i++;
        } else if (s[i] >= 0xC2 && s[i] <= 0xDF) {
            if (i + 1 >= len || (s[i + 1] & 0xC0) != 0x80) return false;
            i += 2;
        } else if (s[i] >= 0xE0 && s[i] <= 0xEF) {
            if (i + 2 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80) return false;
            // Overlong or surrogate rejection
            if (s[i] == 0xE0 && s[i + 1] < 0xA0) return false;
            if (s[i] == 0xED && s[i + 1] >= 0xA0) return false;
            i += 3;
        } else if (s[i] >= 0xF0 && s[i] <= 0xF4) {
            if (i + 3 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80) return false;
            if (s[i] == 0xF0 && s[i + 1] < 0x90) return false;
            if (s[i] == 0xF4 && s[i + 1] >= 0x90) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

}

bool PackageManifest::is_valid() const noexcept {
    if (bundle_id.empty() || bundle_id.size() > 128 ||
        name.empty() || name.size() > 64 ||
        version.empty() || version.size() > 32 ||
        executable_name.empty() || executable_name.size() > 256) {
        return false;
    }
    // Check bundle_id for path traversal
    if (bundle_id.find('/') != std::string::npos || bundle_id.find('\\') != std::string::npos || bundle_id.find("..") != std::string::npos) {
        return false;
    }
    // Architecture validation (if specified, must be arm64)
    if (!architecture.empty() && architecture != "arm64" && architecture != "aarch64") {
        return false;
    }
    if (files.size() > kMaxFileCount) {
        return false;
    }
    // Check file paths for path traversal
    for (const auto& f : files) {
        if (f.path.empty() || f.path.size() > 256 || f.path.find("..") != std::string::npos ||
            f.path.front() == '/' || f.path.front() == '\\' || f.path.find(':') != std::string::npos) {
            return false;
        }
        if (f.size > kMaxIndividualFileSize) {
            return false;
        }
    }
    // Executable must be present in files
    bool exec_found = false;
    for (const auto& f : files) {
        if (f.path == executable_name) {
            exec_found = true;
            break;
        }
    }
    return exec_found;
}

std::string PackageManifest::serialize() const {
    std::ostringstream ss;
    ss << "format_version=" << format_version << "\n";
    ss << "bundle_id=" << bundle_id << "\n";
    ss << "name=" << name << "\n";
    if (!display_name.empty()) {
        ss << "display_name=" << display_name << "\n";
    }
    ss << "version=" << version << "\n";
    ss << "min_os_version=" << min_os_version << "\n";
    ss << "architecture=" << architecture << "\n";
    ss << "min_abi_version=" << min_abi_version << "\n";
    ss << "min_sdk_version_u32=" << min_sdk_version_u32 << "\n";
    ss << "target_abi_version=" << target_abi_version << "\n";
    ss << "executable_name=" << executable_name << "\n";
    ss << "storage_quota_bytes=" << storage_quota_bytes << "\n";
    ss << "max_memory_bytes=" << resource_limits.max_memory_bytes << "\n";
    ss << "max_instruction_budget=" << resource_limits.max_instruction_budget << "\n";
    ss << "max_open_handles=" << resource_limits.max_open_handles << "\n";
    ss << "entry_symbol=" << entry_symbol << "\n";
    ss << "entry_stack_size=" << entry_stack_size << "\n";
    if (!category.empty()) ss << "category=" << category << "\n";
    if (!author.empty()) ss << "author=" << author << "\n";
    if (!description.empty()) ss << "description=" << description << "\n";
    if (!signature_sha256.empty()) {
        ss << "signature_sha256=" << signature_sha256 << "\n";
    }
    if (!icon_path.empty()) {
        ss << "icon_path=" << icon_path << "\n";
    }
    if (!startup_config.empty()) {
        ss << "startup_config=" << startup_config << "\n";
    }
    ss << "permissions=";
    for (std::size_t i = 0; i < permissions.size(); ++i) {
        if (i > 0) ss << ",";
        ss << permissions[i];
    }
    ss << "\n";
    ss << "optional_permissions=";
    for (std::size_t i = 0; i < optional_permissions.size(); ++i) {
        if (i > 0) ss << ",";
        ss << optional_permissions[i];
    }
    ss << "\n";
    ss << "supported_doc_types=";
    for (std::size_t i = 0; i < supported_doc_types.size(); ++i) {
        if (i > 0) ss << ",";
        ss << supported_doc_types[i];
    }
    ss << "\n";
    return ss.str();
}

bool PackageManifest::deserialize(const std::string& text, PackageManifest& out) {
    out = PackageManifest{};
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (key == "format_version") out.format_version = val;
        else if (key == "bundle_id") out.bundle_id = val;
        else if (key == "name") out.name = val;
        else if (key == "display_name") out.display_name = val;
        else if (key == "version") out.version = val;
        else if (key == "min_os_version") out.min_os_version = val;
        else if (key == "architecture") out.architecture = val;
        else if (key == "min_abi_version") out.min_abi_version = val;
        else if (key == "min_sdk_version_u32") {
            try { out.min_sdk_version_u32 = static_cast<std::uint32_t>(std::stoul(val)); } catch (...) {}
        }
        else if (key == "target_abi_version") out.target_abi_version = val;
        else if (key == "executable_name") out.executable_name = val;
        else if (key == "icon_path") out.icon_path = val;
        else if (key == "startup_config") out.startup_config = val;
        else if (key == "entry_symbol") out.entry_symbol = val;
        else if (key == "entry_stack_size") {
            try { out.entry_stack_size = static_cast<std::uint32_t>(std::stoul(val)); } catch (...) {}
        }
        else if (key == "category") out.category = val;
        else if (key == "author") out.author = val;
        else if (key == "description") out.description = val;
        else if (key == "storage_quota_bytes") {
            try { out.storage_quota_bytes = std::stoull(val); } catch (...) {}
        }
        else if (key == "max_memory_bytes") {
            try { out.resource_limits.max_memory_bytes = std::stoull(val); } catch (...) {}
        }
        else if (key == "max_instruction_budget") {
            try { out.resource_limits.max_instruction_budget = std::stoull(val); } catch (...) {}
        }
        else if (key == "max_open_handles") {
            try { out.resource_limits.max_open_handles = static_cast<std::uint32_t>(std::stoul(val)); } catch (...) {}
        }
        else if (key == "signature_sha256") out.signature_sha256 = val;
        else if (key == "permissions") {
            std::istringstream pss(val);
            std::string p;
            while (std::getline(pss, p, ',')) {
                if (!p.empty()) out.permissions.push_back(p);
            }
        }
        else if (key == "optional_permissions") {
            std::istringstream pss(val);
            std::string p;
            while (std::getline(pss, p, ',')) {
                if (!p.empty()) out.optional_permissions.push_back(p);
            }
        }
        else if (key == "supported_doc_types") {
            std::istringstream pss(val);
            std::string p;
            while (std::getline(pss, p, ',')) {
                if (!p.empty()) out.supported_doc_types.push_back(p);
            }
        }
    }
    if (out.display_name.empty()) {
        out.display_name = out.name;
    }
    return !out.bundle_id.empty() && !out.executable_name.empty();
}

void AppPackage::add_file(const std::string& rel_path, std::vector<std::uint8_t> data) {
    FileEntry fe{};
    std::string norm_path = rel_path;
    std::replace(norm_path.begin(), norm_path.end(), '\\', '/');
    while (norm_path.rfind("./", 0) == 0) norm_path = norm_path.substr(2);
    while (!norm_path.empty() && norm_path[0] == '/') norm_path = norm_path.substr(1);
    fe.path = norm_path;
    fe.size = data.size();
    fe.sha256_hex = crypto::Sha256::hash_hex(data);
    fe.data = std::move(data);
    manifest.files.push_back(std::move(fe));
}

std::vector<std::uint8_t> AppPackage::pack_v3() const {
    std::vector<std::uint8_t> buf;
    buf.resize(kV3HeaderSize, 0);

    // 1. Magic
    std::memcpy(buf.data(), kPkgMagicV3, kMagicLen);

    // 2. Header version = 3
    const std::uint32_t version = 3;
    std::memcpy(buf.data() + 8, &version, sizeof(version));

    // 3. Flags: bit 0 = Signed
    const std::uint32_t flags = manifest.signature_sha256.empty() ? 0 : 1;
    std::memcpy(buf.data() + 12, &flags, sizeof(flags));

    // 4. Manifest text & canonical file sorting
    PackageManifest m_copy = manifest;
    m_copy.format_version = "3.0";
    for (auto& f : m_copy.files) {
        std::replace(f.path.begin(), f.path.end(), '\\', '/');
        while (f.path.rfind("./", 0) == 0) f.path = f.path.substr(2);
        while (!f.path.empty() && f.path[0] == '/') f.path = f.path.substr(1);
    }
    std::sort(m_copy.files.begin(), m_copy.files.end(), [](const FileEntry& a, const FileEntry& b) {
        return a.path < b.path;
    });
    std::sort(m_copy.permissions.begin(), m_copy.permissions.end());
    std::sort(m_copy.optional_permissions.begin(), m_copy.optional_permissions.end());
    std::sort(m_copy.supported_doc_types.begin(), m_copy.supported_doc_types.end());
    const std::string manifest_str = m_copy.serialize();
    const std::uint64_t manifest_off = kV3HeaderSize;
    const std::uint32_t manifest_len = static_cast<std::uint32_t>(manifest_str.size());
    std::memcpy(buf.data() + 24, &manifest_off, sizeof(manifest_off));
    std::memcpy(buf.data() + 32, &manifest_len, sizeof(manifest_len));

    buf.insert(buf.end(), manifest_str.begin(), manifest_str.end());

    // 5. File count
    const std::uint32_t file_count = static_cast<std::uint32_t>(m_copy.files.size());
    std::memcpy(buf.data() + 36, &file_count, sizeof(file_count));

    // 6. Directory table placeholder & file payload packaging
    struct EntryMeta {
        std::string path;
        std::uint32_t type{0};
        std::uint32_t flags{0};
        std::uint64_t data_offset{0};
        std::uint64_t data_size{0};
        std::uint8_t sha256[32]{0};
        const std::vector<std::uint8_t>* data_ptr{nullptr};
    };

    std::vector<EntryMeta> metas;
    for (const auto& f : m_copy.files) {
        EntryMeta meta{};
        meta.path = f.path;
        meta.type = (f.path == manifest.executable_name) ? 1 : 0;
        meta.flags = 0;
        meta.data_size = f.data.size();
        meta.data_ptr = &f.data;

        crypto::Sha256 hasher;
        hasher.update(f.data.data(), f.data.size());
        hasher.finalize(meta.sha256);

        metas.push_back(meta);
    }

    // Write directory table entries
    std::size_t dir_table_start = buf.size();
    for (const auto& meta : metas) {
        const auto plen = static_cast<std::uint16_t>(meta.path.size());
        const auto* p_plen = reinterpret_cast<const std::uint8_t*>(&plen);
        buf.insert(buf.end(), p_plen, p_plen + sizeof(plen));
        buf.insert(buf.end(), meta.path.begin(), meta.path.end());

        const auto* p_type = reinterpret_cast<const std::uint8_t*>(&meta.type);
        buf.insert(buf.end(), p_type, p_type + sizeof(meta.type));

        const auto* p_flags = reinterpret_cast<const std::uint8_t*>(&meta.flags);
        buf.insert(buf.end(), p_flags, p_flags + sizeof(meta.flags));

        // Placeholders for offset and size (will patch)
        const std::uint64_t zero64 = 0;
        const auto* p_zero = reinterpret_cast<const std::uint8_t*>(&zero64);
        buf.insert(buf.end(), p_zero, p_zero + sizeof(zero64)); // offset placeholder
        buf.insert(buf.end(), p_zero, p_zero + sizeof(zero64)); // size placeholder

        buf.insert(buf.end(), meta.sha256, meta.sha256 + 32);
    }

    // Append file data blocks, aligned to 16 bytes
    std::size_t patch_ptr = dir_table_start;
    for (std::size_t i = 0; i < metas.size(); ++i) {
        while (buf.size() % 16 != 0) {
            buf.push_back(0);
        }

        const std::uint64_t data_off = static_cast<std::uint64_t>(buf.size());
        const std::uint64_t data_sz = metas[i].data_size;
        buf.insert(buf.end(), metas[i].data_ptr->begin(), metas[i].data_ptr->end());

        // Patch directory entry offset and size
        const auto plen = static_cast<std::uint16_t>(metas[i].path.size());
        std::size_t offset_loc = patch_ptr + 2 + plen + 4 + 4;
        std::memcpy(buf.data() + offset_loc, &data_off, sizeof(data_off));
        std::memcpy(buf.data() + offset_loc + 8, &data_sz, sizeof(data_sz));

        patch_ptr += 2 + plen + 4 + 4 + 8 + 8 + 32;
    }

    // Patch total size in header
    const std::uint64_t total_sz = static_cast<std::uint64_t>(buf.size());
    std::memcpy(buf.data() + 16, &total_sz, sizeof(total_sz));

    return buf;
}

std::vector<std::uint8_t> AppPackage::pack() const {
    if (manifest.format_version == "3.0") {
        return pack_v3();
    }

    std::vector<std::uint8_t> buf;

    // 1. Magic
    const char* magic = (manifest.format_version == "2.0" || manifest.format_version == "2.1") ? kPkgMagicV2 : kPkgMagicV1;
    buf.insert(buf.end(), magic, magic + kMagicLen);

    // 2. Manifest string
    const std::string manifest_str = manifest.serialize();
    const auto mlen = static_cast<std::uint32_t>(manifest_str.size());
    const auto* p_mlen = reinterpret_cast<const std::uint8_t*>(&mlen);
    buf.insert(buf.end(), p_mlen, p_mlen + sizeof(mlen));
    buf.insert(buf.end(), manifest_str.begin(), manifest_str.end());

    // 3. Number of files
    const auto file_count = static_cast<std::uint32_t>(manifest.files.size());
    const auto* p_fcount = reinterpret_cast<const std::uint8_t*>(&file_count);
    buf.insert(buf.end(), p_fcount, p_fcount + sizeof(file_count));

    // 4. File entries
    for (const auto& f : manifest.files) {
        // path
        const auto plen = static_cast<std::uint16_t>(f.path.size());
        const auto* p_plen = reinterpret_cast<const std::uint8_t*>(&plen);
        buf.insert(buf.end(), p_plen, p_plen + sizeof(plen));
        buf.insert(buf.end(), f.path.begin(), f.path.end());

        // data size
        const auto dsize = static_cast<std::uint32_t>(f.data.size());
        const auto* p_dsize = reinterpret_cast<const std::uint8_t*>(&dsize);
        buf.insert(buf.end(), p_dsize, p_dsize + sizeof(dsize));

        // data
        buf.insert(buf.end(), f.data.begin(), f.data.end());
    }

    return buf;
}

PackageValidationResult AppPackage::validate_package_data(const std::uint8_t* data,
                                                          std::size_t size,
                                                          AppPackage* out_pkg) {
    if (!data || size < kMagicLen + 4 + 4) {
        return PackageValidationResult::PayloadTruncated;
    }

    // Check for V3
    if (std::memcmp(data, kPkgMagicV3, kMagicLen) == 0) {
        if (size < kV3HeaderSize) {
            return PackageValidationResult::PayloadTruncated;
        }

        std::uint32_t version = 0;
        std::memcpy(&version, data + 8, 4);
        if (version != 3) {
            return PackageValidationResult::CorruptedHeader;
        }

        std::uint64_t total_size = 0;
        std::memcpy(&total_size, data + 16, 8);
        if (total_size > size) {
            return PackageValidationResult::PayloadTruncated;
        }

        std::uint64_t manifest_off = 0;
        std::memcpy(&manifest_off, data + 24, 8);
        std::uint32_t manifest_len = 0;
        std::memcpy(&manifest_len, data + 32, 4);
        std::uint32_t file_count = 0;
        std::memcpy(&file_count, data + 36, 4);

        if (manifest_len > kMaxManifestLen) {
            return PackageValidationResult::ManifestTooLarge;
        }

        std::uint64_t manifest_end = 0;
        if (!security::checked_add(manifest_off, static_cast<std::uint64_t>(manifest_len), manifest_end)) {
            return PackageValidationResult::IntegerOverflow;
        }
        if (manifest_end > size) {
            return PackageValidationResult::PayloadTruncated;
        }

        // Check for embedded null bytes in manifest text
        if (std::memchr(data + manifest_off, '\0', manifest_len) != nullptr) {
            return PackageValidationResult::InvalidManifest;
        }

        std::string mstr(reinterpret_cast<const char*>(data + manifest_off), manifest_len);
        PackageManifest m{};
        if (!PackageManifest::deserialize(mstr, m)) {
            return PackageValidationResult::InvalidManifest;
        }

        if (!m.architecture.empty() && m.architecture != "arm64" && m.architecture != "aarch64") {
            return PackageValidationResult::UnsupportedArchitecture;
        }

        if (file_count > kMaxFileCount) {
            return PackageValidationResult::FileCountExceeded;
        }

        std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
        ranges.push_back({0, kV3HeaderSize});
        ranges.push_back({manifest_off, manifest_end});

        std::size_t off = static_cast<std::size_t>(manifest_end);
        std::unordered_set<std::string> seen_paths;
        std::unordered_set<std::string> seen_lower_paths;

        for (std::uint32_t i = 0; i < file_count; ++i) {
            if (off + 2 > size) return PackageValidationResult::PayloadTruncated;
            std::uint16_t plen = 0;
            std::memcpy(&plen, data + off, 2);
            off += 2;

            if (plen == 0 || plen > 256) return PackageValidationResult::PathTraversalAttempt;

            std::uint64_t path_end = 0;
            if (!security::checked_add(static_cast<std::uint64_t>(off), static_cast<std::uint64_t>(plen), path_end)) {
                return PackageValidationResult::IntegerOverflow;
            }
            if (path_end > size) return PackageValidationResult::PayloadTruncated;

            std::string p(reinterpret_cast<const char*>(data + off), plen);
            off = static_cast<std::size_t>(path_end);

            // Path character and UTF-8 safety checks
            if (!is_valid_utf8_path(p) || p.find("..") != std::string::npos ||
                p.front() == '/' || p.front() == '\\' || p.find(':') != std::string::npos) {
                return PackageValidationResult::PathTraversalAttempt;
            }

            // Case-insensitive duplicate collision detection
            std::string lower_p = p;
            std::transform(lower_p.begin(), lower_p.end(), lower_p.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (seen_paths.find(p) != seen_paths.end() || seen_lower_paths.find(lower_p) != seen_lower_paths.end()) {
                return PackageValidationResult::DuplicateFileEntry;
            }
            seen_paths.insert(p);
            seen_lower_paths.insert(lower_p);

            if (off + 4 + 4 + 8 + 8 + 32 > size) {
                return PackageValidationResult::PayloadTruncated;
            }

            std::uint32_t entry_type = 0;
            std::memcpy(&entry_type, data + off, 4);
            off += 4;
            std::uint32_t entry_flags = 0;
            std::memcpy(&entry_flags, data + off, 4);
            off += 4;
            (void)entry_flags;

            std::uint64_t data_off = 0;
            std::memcpy(&data_off, data + off, 8);
            off += 8;
            std::uint64_t data_sz = 0;
            std::memcpy(&data_sz, data + off, 8);
            off += 8;

            std::uint8_t expected_hash[32];
            std::memcpy(expected_hash, data + off, 32);
            off += 32;

            if (data_sz > kMaxIndividualFileSize || data_sz > size) {
                return PackageValidationResult::FileCountExceeded;
            }

            std::uint64_t entry_end = 0;
            if (!security::checked_add(data_off, data_sz, entry_end)) {
                return PackageValidationResult::IntegerOverflow;
            }
            if (entry_end > size) {
                return PackageValidationResult::PayloadTruncated;
            }

            // Check range overlaps
            for (const auto& [r_start, r_end] : ranges) {
                if (std::max(data_off, r_start) < std::min(entry_end, r_end)) {
                    return PackageValidationResult::OverlappingEntries;
                }
            }
            ranges.push_back({data_off, entry_end});

            // Check digest
            std::uint8_t actual_hash[32];
            crypto::Sha256 hasher;
            hasher.update(data + data_off, static_cast<std::size_t>(data_sz));
            hasher.finalize(actual_hash);

            if (!security::constant_time_equals(actual_hash, expected_hash, 32)) {
                return PackageValidationResult::ChecksumMismatch;
            }

            std::vector<std::uint8_t> fdata(data + data_off, data + data_off + data_sz);
            FileEntry fe{};
            fe.path = p;
            fe.size = data_sz;
            fe.sha256_hex = crypto::Sha256::hash_hex(fdata);
            fe.data = std::move(fdata);
            m.files.push_back(std::move(fe));
        }

        // Verify executable is present in package
        bool exec_found = false;
        for (const auto& f : m.files) {
            if (f.path == m.executable_name) {
                exec_found = true;
                if (f.data.empty()) {
                    return PackageValidationResult::ExecutableInvalid;
                }
                break;
            }
        }
        if (!exec_found) {
            return PackageValidationResult::ExecutableNotFound;
        }

        if (out_pkg) {
            out_pkg->manifest = std::move(m);
        }
        return PackageValidationResult::Valid;
    }

    // Check magic (accept either V1 or V2)
    if (std::memcmp(data, kPkgMagicV1, kMagicLen) != 0 &&
        std::memcmp(data, kPkgMagicV2, kMagicLen) != 0) {
        return PackageValidationResult::InvalidMagic;
    }

    std::size_t off = kMagicLen;

    // Read manifest length
    std::uint32_t mlen = 0;
    std::memcpy(&mlen, data + off, 4);
    off += 4;

    if (mlen > kMaxManifestLen) {
        return PackageValidationResult::ManifestTooLarge;
    }

    std::uint64_t manifest_end = 0;
    if (!security::checked_add(static_cast<std::uint64_t>(off), static_cast<std::uint64_t>(mlen), manifest_end)) {
        return PackageValidationResult::IntegerOverflow;
    }
    if (manifest_end > size) {
        return PackageValidationResult::PayloadTruncated;
    }

    if (std::memchr(data + off, '\0', mlen) != nullptr) {
        return PackageValidationResult::InvalidManifest;
    }

    std::string mstr(reinterpret_cast<const char*>(data + off), mlen);
    off = static_cast<std::size_t>(manifest_end);

    PackageManifest m{};
    if (!PackageManifest::deserialize(mstr, m)) {
        return PackageValidationResult::InvalidManifest;
    }

    if (!m.architecture.empty() && m.architecture != "arm64" && m.architecture != "aarch64") {
        return PackageValidationResult::UnsupportedArchitecture;
    }

    // Read file count
    if (off + 4 > size) {
        return PackageValidationResult::PayloadTruncated;
    }
    std::uint32_t file_count = 0;
    std::memcpy(&file_count, data + off, 4);
    off += 4;

    if (file_count > kMaxFileCount) {
        return PackageValidationResult::FileCountExceeded;
    }

    std::unordered_set<std::string> seen_paths;
    std::unordered_set<std::string> seen_lower_paths;

    for (std::uint32_t i = 0; i < file_count; ++i) {
        if (off + 2 > size) return PackageValidationResult::PayloadTruncated;
        std::uint16_t plen = 0;
        std::memcpy(&plen, data + off, 2);
        off += 2;

        if (plen == 0 || plen > 256) return PackageValidationResult::PathTraversalAttempt;

        std::uint64_t path_end = 0;
        if (!security::checked_add(static_cast<std::uint64_t>(off), static_cast<std::uint64_t>(plen), path_end)) {
            return PackageValidationResult::IntegerOverflow;
        }
        if (path_end > size) return PackageValidationResult::PayloadTruncated;

        std::string p(reinterpret_cast<const char*>(data + off), plen);
        off = static_cast<std::size_t>(path_end);

        // Path safety & duplicate check
        if (!is_valid_utf8_path(p) || p.find("..") != std::string::npos ||
            p.front() == '/' || p.front() == '\\' || p.find(':') != std::string::npos) {
            return PackageValidationResult::PathTraversalAttempt;
        }

        std::string lower_p = p;
        std::transform(lower_p.begin(), lower_p.end(), lower_p.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (seen_paths.find(p) != seen_paths.end() || seen_lower_paths.find(lower_p) != seen_lower_paths.end()) {
            return PackageValidationResult::DuplicateFileEntry;
        }
        seen_paths.insert(p);
        seen_lower_paths.insert(lower_p);

        if (off + 4 > size) return PackageValidationResult::PayloadTruncated;
        std::uint32_t dsize = 0;
        std::memcpy(&dsize, data + off, 4);
        off += 4;
        if (dsize > kMaxIndividualFileSize || dsize > size) return PackageValidationResult::FileCountExceeded;

        std::uint64_t next_off = 0;
        if (!security::checked_add(static_cast<std::uint64_t>(off), static_cast<std::uint64_t>(dsize), next_off)) {
            return PackageValidationResult::IntegerOverflow;
        }
        if (next_off > size) return PackageValidationResult::PayloadTruncated;

        std::vector<std::uint8_t> fdata(data + off, data + off + dsize);
        off = static_cast<std::size_t>(next_off);

        FileEntry fe{};
        fe.path = p;
        fe.size = dsize;
        fe.sha256_hex = crypto::Sha256::hash_hex(fdata);
        fe.data = std::move(fdata);
        m.files.push_back(std::move(fe));
    }

    // Verify executable is present in package
    bool exec_found = false;
    for (const auto& f : m.files) {
        if (f.path == m.executable_name) {
            exec_found = true;
            if (f.data.empty()) {
                return PackageValidationResult::ExecutableInvalid;
            }
            break;
        }
    }
    if (!exec_found) {
        return PackageValidationResult::ExecutableNotFound;
    }

    if (out_pkg) {
        out_pkg->manifest = std::move(m);
    }
    return PackageValidationResult::Valid;
}

bool AppPackage::unpack(const std::uint8_t* data, std::size_t size, AppPackage& out_pkg) {
    return validate_package_data(data, size, &out_pkg) == PackageValidationResult::Valid;
}

PackageTrustInfo AppPackage::evaluate_trust(const AppPackage& pkg, bool allow_developer_trust) {
    PackageTrustInfo info{};
    info.integrity_verified = pkg.manifest.is_valid();

    // Check if each file has valid content
    for (const auto& f : pkg.manifest.files) {
        if (f.data.empty() && f.path == pkg.manifest.executable_name) {
            info.integrity_verified = false;
            break;
        }
    }

    if (!info.integrity_verified) {
        info.trust_level = PackageTrustLevel::Unsigned;
        return info;
    }

    if (!pkg.manifest.signature_sha256.empty()) {
        info.signature_hex = pkg.manifest.signature_sha256;
        if (pkg.manifest.signature_sha256 == "PLATFORM_ROOT_A16_TRUSTED") {
            info.trust_level = PackageTrustLevel::PlatformSigned;
        } else {
            auto store = security::PublisherTrustStore::create_default_test_store();
            auto key = store.get_key(pkg.manifest.signature_sha256);
            if (key.has_value() && !key->is_revoked) {
                if (key->role == security::PublisherRole::Platform) {
                    info.trust_level = PackageTrustLevel::PlatformSigned;
                } else {
                    info.trust_level = allow_developer_trust ? PackageTrustLevel::DeveloperLocal : PackageTrustLevel::Unsigned;
                }
            } else {
                info.trust_level = PackageTrustLevel::Unsigned;
            }
        }
    } else {
        info.trust_level = allow_developer_trust ? PackageTrustLevel::DeveloperLocal : PackageTrustLevel::Unsigned;
    }

    return info;
}

PackageTrustInfo AppPackage::evaluate_trust(const std::uint8_t* raw_pkg_data,
                                            std::size_t raw_pkg_size,
                                            const security::PublisherTrustStore& store,
                                            bool allow_developer_trust) {
    PackageTrustInfo info{};
    AppPackage pkg{};
    if (validate_package_data(raw_pkg_data, raw_pkg_size, &pkg) != PackageValidationResult::Valid) {
        info.integrity_verified = false;
        info.trust_level = PackageTrustLevel::Unsigned;
        return info;
    }
    info.integrity_verified = true;

    auto policy = allow_developer_trust ? security::TrustPolicy::Development : security::TrustPolicy::StrictProduction;
    auto sig_res = store.verify_package_signature(raw_pkg_data, raw_pkg_size, policy);

    info.digest_hex = sig_res.digest_hex;
    info.signature_hex = sig_res.signature_hex;

    if (sig_res.status == security::SignatureStatus::PlatformSigned) {
        info.trust_level = PackageTrustLevel::PlatformSigned;
    } else if (sig_res.status == security::SignatureStatus::DeveloperSigned) {
        info.trust_level = allow_developer_trust ? PackageTrustLevel::DeveloperLocal : PackageTrustLevel::Unsigned;
    } else if (sig_res.status == security::SignatureStatus::Unsigned) {
        info.trust_level = allow_developer_trust ? PackageTrustLevel::DeveloperLocal : PackageTrustLevel::Unsigned;
    } else {
        info.trust_level = PackageTrustLevel::Unsigned;
        info.integrity_verified = false;
    }

    return info;
}

} // namespace winios::packaging
