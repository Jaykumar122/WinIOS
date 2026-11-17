#include "packaging/app_package.h"
#include "crypto/sha256.h"

#include <cstring>
#include <sstream>

namespace winios::packaging {

namespace {
constexpr const char* kPkgMagic = "WIOSPKG1";
constexpr std::size_t kMagicLen = 8;
}

bool PackageManifest::is_valid() const noexcept {
    if (bundle_id.empty() || name.empty() || version.empty() || executable_name.empty()) {
        return false;
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
    ss << "bundle_id=" << bundle_id << "\n";
    ss << "name=" << name << "\n";
    ss << "version=" << version << "\n";
    ss << "min_os_version=" << min_os_version << "\n";
    ss << "executable_name=" << executable_name << "\n";
    ss << "permissions=";
    for (std::size_t i = 0; i < permissions.size(); ++i) {
        if (i > 0) ss << ",";
        ss << permissions[i];
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

        if (key == "bundle_id") out.bundle_id = val;
        else if (key == "name") out.name = val;
        else if (key == "version") out.version = val;
        else if (key == "min_os_version") out.min_os_version = val;
        else if (key == "executable_name") out.executable_name = val;
        else if (key == "permissions") {
            std::istringstream pss(val);
            std::string p;
            while (std::getline(pss, p, ',')) {
                if (!p.empty()) out.permissions.push_back(p);
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
    buf.insert(buf.end(), kPkgMagic, kPkgMagic + kMagicLen);

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

bool AppPackage::unpack(const std::uint8_t* data, std::size_t size, AppPackage& out_pkg) {
    if (!data || size < kMagicLen + 4 + 4) return false;

    // Check magic
    if (std::memcmp(data, kPkgMagic, kMagicLen) != 0) return false;

    std::size_t off = kMagicLen;

    // Read manifest length
    std::uint32_t mlen = 0;
    std::memcpy(&mlen, data + off, 4);
    off += 4;
    if (off + mlen > size) return false;

    std::string mstr(reinterpret_cast<const char*>(data + off), mlen);
    off += mlen;

    PackageManifest m{};
    if (!PackageManifest::deserialize(mstr, m)) return false;

    // Read file count
    if (off + 4 > size) return false;
    std::uint32_t file_count = 0;
    std::memcpy(&file_count, data + off, 4);
    off += 4;

    for (std::uint32_t i = 0; i < file_count; ++i) {
        if (off + 2 > size) return false;
        std::uint16_t plen = 0;
        std::memcpy(&plen, data + off, 2);
        off += 2;
        if (off + plen > size) return false;

        std::string p(reinterpret_cast<const char*>(data + off), plen);
        off += plen;

        if (off + 4 > size) return false;
        std::uint32_t dsize = 0;
        std::memcpy(&dsize, data + off, 4);
        off += 4;
        if (off + dsize > size) return false;

        std::vector<std::uint8_t> fdata(data + off, data + off + dsize);
        off += dsize;

        FileEntry fe{};
        fe.path = p;
        fe.size = dsize;
        fe.sha256_hex = crypto::Sha256::hash_hex(fdata);
        fe.data = std::move(fdata);
        m.files.push_back(std::move(fe));
    }

    out_pkg.manifest = std::move(m);
    return true;
}

} // namespace winios::packaging
