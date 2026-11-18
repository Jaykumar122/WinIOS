#include "packaging/app_package.h"
#include "crypto/sha256.h"

#include <cstring>
#include <sstream>
#include <unordered_set>

namespace winios::packaging {

namespace {
constexpr const char* kPkgMagicV1 = "WIOSPKG1";
constexpr const char* kPkgMagicV2 = "WIOSPKG2";
constexpr std::size_t kMagicLen = 8;
constexpr std::size_t kMaxManifestLen = 65536;
constexpr std::size_t kMaxFileCount = 256;
constexpr std::size_t kMaxIndividualFileSize = 32 * 1024 * 1024; // 32 MB
}

bool PackageManifest::is_valid() const noexcept {
    if (bundle_id.empty() || name.empty() || version.empty() || executable_name.empty()) {
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
    // Check file paths for path traversal
    for (const auto& f : files) {
        if (f.path.empty() || f.path.find("..") != std::string::npos || f.path.front() == '/' || f.path.front() == '\\') {
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
    ss << "version=" << version << "\n";
    ss << "min_os_version=" << min_os_version << "\n";
    ss << "architecture=" << architecture << "\n";
    ss << "min_abi_version=" << min_abi_version << "\n";
    ss << "executable_name=" << executable_name << "\n";
    ss << "storage_quota_bytes=" << storage_quota_bytes << "\n";
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
        else if (key == "version") out.version = val;
        else if (key == "min_os_version") out.min_os_version = val;
        else if (key == "architecture") out.architecture = val;
        else if (key == "min_abi_version") out.min_abi_version = val;
        else if (key == "executable_name") out.executable_name = val;
        else if (key == "icon_path") out.icon_path = val;
        else if (key == "startup_config") out.startup_config = val;
        else if (key == "storage_quota_bytes") {
            try { out.storage_quota_bytes = std::stoull(val); } catch (...) {}
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
    }
    return !out.bundle_id.empty() && !out.executable_name.empty();
}

void AppPackage::add_file(const std::string& rel_path, std::vector<std::uint8_t> data) {
    FileEntry fe{};
    fe.path = rel_path;
    fe.size = data.size();
    fe.sha256_hex = crypto::Sha256::hash_hex(data);
    fe.data = std::move(data);
    manifest.files.push_back(std::move(fe));
}

std::vector<std::uint8_t> AppPackage::pack() const {
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
    if (off + mlen > size) {
        return PackageValidationResult::PayloadTruncated;
    }

    std::string mstr(reinterpret_cast<const char*>(data + off), mlen);
    off += mlen;

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

    for (std::uint32_t i = 0; i < file_count; ++i) {
        if (off + 2 > size) return PackageValidationResult::PayloadTruncated;
        std::uint16_t plen = 0;
        std::memcpy(&plen, data + off, 2);
        off += 2;
        if (off + plen > size) return PackageValidationResult::PayloadTruncated;

        std::string p(reinterpret_cast<const char*>(data + off), plen);
        off += plen;

        // Path safety & duplicate check
        if (p.empty() || p.find("..") != std::string::npos || p.front() == '/' || p.front() == '\\' || p.find(':') != std::string::npos) {
            return PackageValidationResult::PathTraversalAttempt;
        }
        if (seen_paths.find(p) != seen_paths.end()) {
            return PackageValidationResult::DuplicateFileEntry;
        }
        seen_paths.insert(p);

        if (off + 4 > size) return PackageValidationResult::PayloadTruncated;
        std::uint32_t dsize = 0;
        std::memcpy(&dsize, data + off, 4);
        off += 4;
        if (dsize > kMaxIndividualFileSize) return PackageValidationResult::FileCountExceeded;
        if (off + dsize > size) return PackageValidationResult::PayloadTruncated;

        std::vector<std::uint8_t> fdata(data + off, data + off + dsize);
        off += dsize;

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

} // namespace winios::packaging
