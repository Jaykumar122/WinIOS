#include "security/publisher_trust_store.h"
#include "crypto/sha256.h"

#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace winios::security {

namespace {
constexpr const char kTrailerMagic[8] = {'W', 'I', 'O', 'S', 'A', 'U', 'T', 'H'};
constexpr std::size_t kTrailerMagicLen = 8;
constexpr std::size_t kMinTrailerLen = 8 + 4 + 4 + 2 + 2 + 8 + 8 + 2 + 32 + 4; // ~74 bytes
constexpr const char kAuthDomainPrefix[] = "WIOS-PKG-AUTH-V1:";
}

void hmac_sha256(const void* key, std::size_t key_len,
                 const void* msg, std::size_t msg_len,
                 std::uint8_t out[32]) noexcept {
    std::uint8_t k_block[64];
    std::memset(k_block, 0, sizeof(k_block));

    if (key_len > 64) {
        crypto::Sha256 k_hasher;
        k_hasher.update(key, key_len);
        k_hasher.finalize(k_block);
    } else if (key_len > 0 && key != nullptr) {
        std::memcpy(k_block, key, key_len);
    }

    std::uint8_t k_ipad[64];
    std::uint8_t k_opad[64];
    for (std::size_t i = 0; i < 64; ++i) {
        k_ipad[i] = k_block[i] ^ 0x36;
        k_opad[i] = k_block[i] ^ 0x5C;
    }

    // Inner hash: H(ipad || msg)
    std::uint8_t inner_hash[32];
    crypto::Sha256 inner_hasher;
    inner_hasher.update(k_ipad, sizeof(k_ipad));
    if (msg_len > 0 && msg != nullptr) {
        inner_hasher.update(msg, msg_len);
    }
    inner_hasher.finalize(inner_hash);

    // Outer hash: H(opad || inner_hash)
    crypto::Sha256 outer_hasher;
    outer_hasher.update(k_opad, sizeof(k_opad));
    outer_hasher.update(inner_hash, sizeof(inner_hash));
    outer_hasher.finalize(out);

    // Wipe sensitive materials
    secure_zero_memory(k_block, sizeof(k_block));
    secure_zero_memory(k_ipad, sizeof(k_ipad));
    secure_zero_memory(k_opad, sizeof(k_opad));
    secure_zero_memory(inner_hash, sizeof(inner_hash));
}

bool PublisherTrustStore::register_key(const PublisherKey& key) {
    if (key.key_id.empty() || key.publisher_id.empty()) {
        return false;
    }
    keys_[key.key_id] = key;
    if (key.is_revoked) {
        revoked_keys_.insert(key.key_id);
    }
    return true;
}

bool PublisherTrustStore::revoke_key(const std::string& key_id) {
    if (key_id.empty()) return false;
    revoked_keys_.insert(key_id);
    auto it = keys_.find(key_id);
    if (it != keys_.end()) {
        it->second.is_revoked = true;
    }
    return true;
}

bool PublisherTrustStore::revoke_publisher(const std::string& publisher_id) {
    if (publisher_id.empty()) return false;
    revoked_publishers_.insert(publisher_id);
    for (auto& [_, k] : keys_) {
        if (k.publisher_id == publisher_id) {
            k.is_revoked = true;
            revoked_keys_.insert(k.key_id);
        }
    }
    return true;
}

bool PublisherTrustStore::is_key_revoked(const std::string& key_id) const {
    if (revoked_keys_.contains(key_id)) return true;
    auto it = keys_.find(key_id);
    if (it != keys_.end()) {
        if (it->second.is_revoked) return true;
        if (revoked_publishers_.contains(it->second.publisher_id)) return true;
    }
    return false;
}

bool PublisherTrustStore::is_publisher_revoked(const std::string& publisher_id) const {
    return revoked_publishers_.contains(publisher_id);
}

std::optional<PublisherKey> PublisherTrustStore::get_key(const std::string& key_id) const {
    auto it = keys_.find(key_id);
    if (it != keys_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string PublisherTrustStore::serialize() const {
    std::ostringstream oss;
    oss << "# WinIOS Publisher Trust Store\n";
    oss << "version=1.0\n";
    for (const auto& [_, k] : keys_) {
        oss << "[key]\n";
        oss << "publisher_id=" << k.publisher_id << "\n";
        oss << "org_name=" << k.organization_name << "\n";
        oss << "key_id=" << k.key_id << "\n";
        oss << "role=" << (k.role == PublisherRole::Platform ? "Platform" : "Developer") << "\n";
        oss << "algorithm=" << (k.algorithm == SignatureAlgorithm::HmacSha256 ? "HmacSha256" : "Ed25519") << "\n";
        oss << "valid_from=" << k.valid_from << "\n";
        oss << "valid_until=" << k.valid_until << "\n";
        oss << "revoked=" << (k.is_revoked ? "1" : "0") << "\n";
        oss << "key_hex=";
        for (std::uint8_t b : k.key_bytes) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        }
        oss << "\n";
    }
    for (const auto& rpub : revoked_publishers_) {
        oss << "revoked_publisher=" << rpub << "\n";
    }
    return oss.str();
}

bool PublisherTrustStore::deserialize(const std::string& text) {
    std::istringstream iss(text);
    std::string line;
    PublisherKey current_key{};
    bool in_key = false;

    auto hex_to_bytes = [](const std::string& hex) -> std::vector<std::uint8_t> {
        std::vector<std::uint8_t> bytes;
        if (hex.size() % 2 != 0) return bytes;
        for (std::size_t i = 0; i < hex.size(); i += 2) {
            std::string byte_str = hex.substr(i, 2);
            char* end = nullptr;
            auto val = static_cast<std::uint8_t>(std::strtoul(byte_str.c_str(), &end, 16));
            if (end != byte_str.c_str() + 2) return {};
            bytes.push_back(val);
        }
        return bytes;
    };

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        if (line == "[key]") {
            if (in_key && !current_key.key_id.empty()) {
                register_key(current_key);
            }
            current_key = PublisherKey{};
            in_key = true;
            continue;
        }

        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        std::string k = line.substr(0, eq_pos);
        std::string v = line.substr(eq_pos + 1);

        if (in_key) {
            if (k == "publisher_id") current_key.publisher_id = v;
            else if (k == "org_name") current_key.organization_name = v;
            else if (k == "key_id") current_key.key_id = v;
            else if (k == "role") current_key.role = (v == "Platform") ? PublisherRole::Platform : PublisherRole::Developer;
            else if (k == "algorithm") current_key.algorithm = (v == "Ed25519") ? SignatureAlgorithm::Ed25519 : SignatureAlgorithm::HmacSha256;
            else if (k == "valid_from") current_key.valid_from = std::strtoull(v.c_str(), nullptr, 10);
            else if (k == "valid_until") current_key.valid_until = std::strtoull(v.c_str(), nullptr, 10);
            else if (k == "revoked") current_key.is_revoked = (v == "1");
            else if (k == "key_hex") current_key.key_bytes = hex_to_bytes(v);
        } else {
            if (k == "revoked_publisher") {
                revoke_publisher(v);
            }
        }
    }
    if (in_key && !current_key.key_id.empty()) {
        register_key(current_key);
    }
    return true;
}

PublisherKey PublisherTrustStore::get_test_platform_key() {
    PublisherKey k;
    k.publisher_id = "com.winios.platform.root";
    k.organization_name = "WinIOS Platform Authority";
    k.key_id = "KEY-PLATFORM-TEST-001";
    k.role = PublisherRole::Platform;
    k.algorithm = SignatureAlgorithm::HmacSha256;
    k.key_bytes.assign(32, 0x11); // Deterministic test key
    k.key_bytes[0] = 0xAA;
    k.key_bytes[31] = 0xBB;
    k.valid_from = 0;
    k.valid_until = 0;
    k.is_revoked = false;
    return k;
}

PublisherKey PublisherTrustStore::get_test_developer_key() {
    PublisherKey k;
    k.publisher_id = "com.developer.sample";
    k.organization_name = "Acme Developer Labs";
    k.key_id = "KEY-DEV-TEST-001";
    k.role = PublisherRole::Developer;
    k.algorithm = SignatureAlgorithm::HmacSha256;
    k.key_bytes.assign(32, 0x22); // Deterministic test key
    k.key_bytes[0] = 0xCC;
    k.key_bytes[31] = 0xDD;
    k.valid_from = 0;
    k.valid_until = 0;
    k.is_revoked = false;
    return k;
}

PublisherKey PublisherTrustStore::get_test_revoked_key() {
    PublisherKey k;
    k.publisher_id = "com.malicious.attacker";
    k.organization_name = "Revoked Publisher Entity";
    k.key_id = "KEY-REVOKED-TEST-999";
    k.role = PublisherRole::Developer;
    k.algorithm = SignatureAlgorithm::HmacSha256;
    k.key_bytes.assign(32, 0x33);
    k.valid_from = 0;
    k.valid_until = 0;
    k.is_revoked = true;
    return k;
}

PublisherTrustStore PublisherTrustStore::create_default_test_store() {
    PublisherTrustStore store;
    store.register_key(get_test_platform_key());
    store.register_key(get_test_developer_key());
    store.register_key(get_test_revoked_key());
    return store;
}

bool PublisherTrustStore::sign_package(std::vector<std::uint8_t>& pkg_data,
                                       const PublisherKey& key,
                                       std::uint64_t valid_from,
                                       std::uint64_t valid_until) {
    if (pkg_data.size() < 48 || key.key_bytes.empty()) {
        return false;
    }

    // Check if package already has a trailer; if so, strip it first
    if (pkg_data.size() >= kMinTrailerLen) {
        std::uint32_t last_len = 0;
        std::memcpy(&last_len, pkg_data.data() + pkg_data.size() - sizeof(last_len), sizeof(last_len));
        if (last_len >= kMinTrailerLen && last_len <= pkg_data.size()) {
            std::size_t candidate_start = pkg_data.size() - last_len;
            if (std::memcmp(pkg_data.data() + candidate_start, kTrailerMagic, kTrailerMagicLen) == 0) {
                pkg_data.resize(candidate_start);
            }
        }
    }

    const std::size_t auth_payload_size = pkg_data.size();

    // Compute trailer total length beforehand so header flags and total_sz can be updated
    // BEFORE calculating the HMAC signature over the authenticated bytes.
    const std::uint32_t trailer_total_len = static_cast<std::uint32_t>(
        kTrailerMagicLen + // 8
        4 + 4 + // version, alg
        2 + key.publisher_id.size() + // pub
        2 + key.key_id.size() + // key
        16 + // valid_from, valid_until
        2 + 32 + // sig_len, sig
        4 // trailer_total_len
    );

    // If WIOSPKG3, set bit 0 (Signed) in header flags and patch total_sz
    if (pkg_data.size() >= 48 && std::memcmp(pkg_data.data(), "WIOSPKG3", 8) == 0) {
        std::uint32_t flags = 0;
        std::memcpy(&flags, pkg_data.data() + 12, sizeof(flags));
        flags |= 0x01; // Signed
        std::memcpy(pkg_data.data() + 12, &flags, sizeof(flags));

        std::uint64_t new_total_sz = auth_payload_size + trailer_total_len;
        std::memcpy(pkg_data.data() + 16, &new_total_sz, sizeof(new_total_sz));
    }

    // 1. Prepare HMAC input
    // Format: "WIOS-PKG-AUTH-V1:" || Payload || pub_id || key_id || valid_from (8B) || valid_until (8B)
    std::vector<std::uint8_t> auth_input;
    const std::size_t prefix_len = std::strlen(kAuthDomainPrefix);
    auth_input.insert(auth_input.end(), kAuthDomainPrefix, kAuthDomainPrefix + prefix_len);
    auth_input.insert(auth_input.end(), pkg_data.begin(), pkg_data.end());
    auth_input.insert(auth_input.end(), key.publisher_id.begin(), key.publisher_id.end());
    auth_input.insert(auth_input.end(), key.key_id.begin(), key.key_id.end());

    std::uint8_t time_buf[16];
    std::memcpy(time_buf, &valid_from, 8);
    std::memcpy(time_buf + 8, &valid_until, 8);
    auth_input.insert(auth_input.end(), time_buf, time_buf + 16);

    std::uint8_t sig[32];
    hmac_sha256(key.key_bytes.data(), key.key_bytes.size(),
                auth_input.data(), auth_input.size(), sig);
    secure_zero_memory(auth_input.data(), auth_input.size());

    // 2. Build Trailer Block
    std::vector<std::uint8_t> trailer;
    trailer.insert(trailer.end(), kTrailerMagic, kTrailerMagic + kTrailerMagicLen);

    const std::uint32_t version = 1;
    const std::uint32_t alg = static_cast<std::uint32_t>(key.algorithm);
    const auto* p_ver = reinterpret_cast<const std::uint8_t*>(&version);
    const auto* p_alg = reinterpret_cast<const std::uint8_t*>(&alg);
    trailer.insert(trailer.end(), p_ver, p_ver + 4);
    trailer.insert(trailer.end(), p_alg, p_alg + 4);

    const auto pub_len = static_cast<std::uint16_t>(key.publisher_id.size());
    const auto* p_pub_len = reinterpret_cast<const std::uint8_t*>(&pub_len);
    trailer.insert(trailer.end(), p_pub_len, p_pub_len + 2);
    trailer.insert(trailer.end(), key.publisher_id.begin(), key.publisher_id.end());

    const auto key_len = static_cast<std::uint16_t>(key.key_id.size());
    const auto* p_key_len = reinterpret_cast<const std::uint8_t*>(&key_len);
    trailer.insert(trailer.end(), p_key_len, p_key_len + 2);
    trailer.insert(trailer.end(), key.key_id.begin(), key.key_id.end());

    trailer.insert(trailer.end(), time_buf, time_buf + 16);

    const std::uint16_t sig_len = 32;
    const auto* p_sig_len = reinterpret_cast<const std::uint8_t*>(&sig_len);
    trailer.insert(trailer.end(), p_sig_len, p_sig_len + 2);
    trailer.insert(trailer.end(), sig, sig + 32);

    const auto* p_tot_len = reinterpret_cast<const std::uint8_t*>(&trailer_total_len);
    trailer.insert(trailer.end(), p_tot_len, p_tot_len + 4);

    // Append trailer
    pkg_data.insert(pkg_data.end(), trailer.begin(), trailer.end());
    return true;
}

SignatureVerificationResult PublisherTrustStore::verify_package_signature(
    const std::uint8_t* pkg_data,
    std::size_t pkg_size,
    TrustPolicy policy,
    std::uint64_t current_time) const {

    SignatureVerificationResult res{};

    if (!pkg_data || pkg_size < 48) {
        res.status = SignatureStatus::Unsigned;
        res.is_trusted = (policy == TrustPolicy::Development);
        res.failure_reason = "Package too small or null";
        return res;
    }

    // Check if package has trailer
    if (pkg_size < kMinTrailerLen) {
        res.status = SignatureStatus::Unsigned;
        res.is_trusted = (policy == TrustPolicy::Development);
        return res;
    }

    std::uint32_t trailer_len = 0;
    std::memcpy(&trailer_len, pkg_data + pkg_size - 4, 4);

    if (trailer_len < kMinTrailerLen || trailer_len > pkg_size || trailer_len > 65536) {
        // Not a signed envelope trailer
        res.status = SignatureStatus::Unsigned;
        res.is_trusted = (policy == TrustPolicy::Development);
        return res;
    }

    const std::size_t trailer_start = pkg_size - trailer_len;
    if (std::memcmp(pkg_data + trailer_start, kTrailerMagic, kTrailerMagicLen) != 0) {
        // No magic match
        res.status = SignatureStatus::Unsigned;
        res.is_trusted = (policy == TrustPolicy::Development);
        return res;
    }

    // Parse trailer using BinaryReader
    BinaryReader reader(pkg_data + trailer_start + kTrailerMagicLen, trailer_len - kTrailerMagicLen - 4);

    std::uint32_t version = 0;
    std::uint32_t alg = 0;
    if (!reader.read_u32_le(version) || !reader.read_u32_le(alg) || version != 1) {
        res.status = SignatureStatus::CorruptSignature;
        res.failure_reason = "Corrupt trailer header or unsupported version";
        return res;
    }

    std::uint16_t pub_len = 0;
    if (!reader.read_u16_le(pub_len)) {
        res.status = SignatureStatus::CorruptSignature;
        res.failure_reason = "Corrupt publisher length";
        return res;
    }
    if (!reader.read_string(res.publisher_id, pub_len)) {
        res.status = SignatureStatus::CorruptSignature;
        res.failure_reason = "Truncated publisher ID";
        return res;
    }

    std::uint16_t key_len = 0;
    if (!reader.read_u16_le(key_len)) {
        res.status = SignatureStatus::CorruptSignature;
        res.failure_reason = "Corrupt key length";
        return res;
    }
    if (!reader.read_string(res.key_id, key_len)) {
        res.status = SignatureStatus::CorruptSignature;
        res.failure_reason = "Truncated key ID";
        return res;
    }

    std::uint64_t valid_from = 0;
    std::uint64_t valid_until = 0;
    if (!reader.read_u64_le(valid_from) || !reader.read_u64_le(valid_until)) {
        res.status = SignatureStatus::CorruptSignature;
        res.failure_reason = "Truncated timestamps";
        return res;
    }

    std::uint16_t sig_len = 0;
    if (!reader.read_u16_le(sig_len) || sig_len != 32) {
        res.status = SignatureStatus::CorruptSignature;
        res.failure_reason = "Invalid signature length";
        return res;
    }

    const std::uint8_t* sig_ptr = nullptr;
    if (!reader.read_slice(sig_ptr, sig_len)) {
        res.status = SignatureStatus::CorruptSignature;
        res.failure_reason = "Truncated signature payload";
        return res;
    }

    // Convert signature to hex for diagnostics
    std::ostringstream sig_hex;
    for (std::size_t i = 0; i < sig_len; ++i) {
        sig_hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(sig_ptr[i]);
    }
    res.signature_hex = sig_hex.str();

    // Check trust store for key
    auto maybe_key = get_key(res.key_id);
    if (!maybe_key.has_value()) {
        res.status = SignatureStatus::UnknownKey;
        res.failure_reason = "Key ID not registered in trust store";
        return res;
    }
    const auto& key = *maybe_key;
    res.role = key.role;

    // Check revocation
    if (key.is_revoked || is_key_revoked(res.key_id) || is_publisher_revoked(res.publisher_id)) {
        res.status = SignatureStatus::RevokedPublisher;
        res.failure_reason = "Key or publisher is revoked";
        return res;
    }

    // Check lifetime
    if (current_time > 0) {
        if ((valid_from > 0 && current_time < valid_from) ||
            (valid_until > 0 && current_time > valid_until)) {
            res.status = SignatureStatus::ExpiredSignature;
            res.failure_reason = "Signature timestamp expired or not yet valid";
            return res;
        }
    }

    // Recompute HMAC-SHA256 signature over exact authenticated byte range [0 .. trailer_start]
    std::vector<std::uint8_t> auth_input;
    const std::size_t prefix_len = std::strlen(kAuthDomainPrefix);
    auth_input.insert(auth_input.end(), kAuthDomainPrefix, kAuthDomainPrefix + prefix_len);
    auth_input.insert(auth_input.end(), pkg_data, pkg_data + trailer_start);
    auth_input.insert(auth_input.end(), res.publisher_id.begin(), res.publisher_id.end());
    auth_input.insert(auth_input.end(), res.key_id.begin(), res.key_id.end());

    std::uint8_t time_buf[16];
    std::memcpy(time_buf, &valid_from, 8);
    std::memcpy(time_buf + 8, &valid_until, 8);
    auth_input.insert(auth_input.end(), time_buf, time_buf + 16);

    std::uint8_t expected_sig[32];
    hmac_sha256(key.key_bytes.data(), key.key_bytes.size(),
                auth_input.data(), auth_input.size(), expected_sig);
    secure_zero_memory(auth_input.data(), auth_input.size());

    // Constant-time compare
    if (!constant_time_equals(sig_ptr, expected_sig, 32)) {
        res.status = SignatureStatus::CorruptSignature;
        res.failure_reason = "Signature cryptographic mismatch or payload tampered";
        return res;
    }

    // Compute payload digest hex for reporting
    res.digest_hex = crypto::Sha256::hash_hex(pkg_data, trailer_start);

    // Signature verified!
    if (key.role == PublisherRole::Platform) {
        res.status = SignatureStatus::PlatformSigned;
        res.is_trusted = true;
    } else {
        res.status = SignatureStatus::DeveloperSigned;
        res.is_trusted = (policy == TrustPolicy::Development);
    }

    return res;
}

} // namespace winios::security
