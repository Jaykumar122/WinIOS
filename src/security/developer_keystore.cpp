#include "security/developer_keystore.h"
#include "security/security_utils.h"
#include "crypto/sha256.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#endif

namespace winios::security {

namespace {

std::string bytes_to_hex(const std::uint8_t* data, std::size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(data[i]);
    }
    return oss.str();
}

std::vector<std::uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<std::uint8_t> bytes;
    if (hex.size() % 2 != 0) return bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        auto byte_str = hex.substr(i, 2);
        char* end = nullptr;
        unsigned long val = std::strtoul(byte_str.c_str(), &end, 16);
        if (end != byte_str.c_str() + 2) {
            bytes.clear();
            return bytes;
        }
        bytes.push_back(static_cast<std::uint8_t>(val));
    }
    return bytes;
}

std::uint64_t current_unix_time() noexcept {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

} // namespace

void DeveloperIdentity::wipe_private_key() noexcept {
    if (!private_key_seed.empty()) {
        secure_zero_memory(private_key_seed.data(), private_key_seed.size());
        private_key_seed.clear();
    }
}

bool DeveloperIdentity::is_valid() const noexcept {
    return !publisher_id.empty() &&
           !key_id.empty() &&
           private_key_seed.size() == 32 &&
           public_key.size() == 32;
}

DeveloperIdentity DeveloperKeyStore::generate_identity(
    const std::string& publisher_id,
    const std::string& organization_name,
    const std::string& key_id,
    PublisherRole role,
    std::uint64_t valid_from,
    std::uint64_t valid_until) {

    DeveloperIdentity identity;
    identity.publisher_id = publisher_id;
    identity.organization_name = organization_name;
    identity.key_id = key_id;
    identity.role = role;
    identity.algorithm = SignatureAlgorithm::Ed25519;
    identity.created_at = current_unix_time();
    identity.valid_from = (valid_from == 0) ? identity.created_at : valid_from;
    identity.valid_until = valid_until;

    // Generate 32 bytes CSPRNG seed
    identity.private_key_seed.resize(32);
    SecureRandom::get_random_bytes(identity.private_key_seed.data(), 32);

    // Compute 32-byte Ed25519 public key
    identity.public_key.resize(32);
    crypto::Ed25519Seed seed_arr;
    std::memcpy(seed_arr.data(), identity.private_key_seed.data(), 32);
    crypto::Ed25519PublicKey pub_arr;
    crypto::Ed25519PrivateKey priv_arr;
    crypto::Ed25519::create_keypair(pub_arr, priv_arr, seed_arr);
    std::memcpy(identity.public_key.data(), pub_arr.data(), 32);

    return identity;
}

PublisherKey DeveloperKeyStore::export_public_key(const DeveloperIdentity& identity) {
    PublisherKey pub;
    pub.publisher_id = identity.publisher_id;
    pub.organization_name = identity.organization_name;
    pub.key_id = identity.key_id;
    pub.role = identity.role;
    pub.algorithm = SignatureAlgorithm::Ed25519;
    // INVARIANT: Key bytes contain ONLY the public key (32 bytes)
    pub.key_bytes = identity.public_key;
    pub.valid_from = identity.valid_from;
    pub.valid_until = identity.valid_until;
    pub.is_revoked = false;
    return pub;
}

std::string DeveloperKeyStore::export_public_key_text(const DeveloperIdentity& identity) {
    std::ostringstream oss;
    oss << "[key]\n";
    oss << "publisher_id=" << identity.publisher_id << "\n";
    oss << "org_name=" << identity.organization_name << "\n";
    oss << "key_id=" << identity.key_id << "\n";
    oss << "role=" << ((identity.role == PublisherRole::Platform) ? "Platform" : "Developer") << "\n";
    oss << "algorithm=Ed25519\n";
    oss << "valid_from=" << identity.valid_from << "\n";
    oss << "valid_until=" << identity.valid_until << "\n";
    oss << "revoked=0\n";
    oss << "key_hex=" << bytes_to_hex(identity.public_key.data(), identity.public_key.size()) << "\n";
    return oss.str();
}

bool DeveloperKeyStore::save_public_key_file(const std::string& file_path,
                                             const DeveloperIdentity& identity) {
    std::string text = export_public_key_text(identity);
    std::ofstream ofs(file_path, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
    return ofs.good();
}

bool DeveloperKeyStore::dpapi_protect(const std::vector<std::uint8_t>& plaintext,
                                      std::vector<std::uint8_t>& ciphertext) {
#ifdef _WIN32
    if (plaintext.empty()) return false;
    DATA_BLOB data_in;
    data_in.pbData = const_cast<BYTE*>(plaintext.data());
    data_in.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB data_out;
    std::memset(&data_out, 0, sizeof(data_out));

    if (!CryptProtectData(&data_in, L"WinIOS Developer Key", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &data_out)) {
        return false;
    }

    ciphertext.assign(data_out.pbData, data_out.pbData + data_out.cbData);
    LocalFree(data_out.pbData);
    return true;
#else
    (void)plaintext;
    (void)ciphertext;
    return false;
#endif
}

bool DeveloperKeyStore::dpapi_unprotect(const std::vector<std::uint8_t>& ciphertext,
                                        std::vector<std::uint8_t>& plaintext) {
#ifdef _WIN32
    if (ciphertext.empty()) return false;
    DATA_BLOB data_in;
    data_in.pbData = const_cast<BYTE*>(ciphertext.data());
    data_in.cbData = static_cast<DWORD>(ciphertext.size());

    DATA_BLOB data_out;
    std::memset(&data_out, 0, sizeof(data_out));

    if (!CryptUnprotectData(&data_in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &data_out)) {
        return false;
    }

    plaintext.assign(data_out.pbData, data_out.pbData + data_out.cbData);
    LocalFree(data_out.pbData);
    return true;
#else
    (void)ciphertext;
    (void)plaintext;
    return false;
#endif
}

void DeveloperKeyStore::pbkdf2_hmac_sha256(const std::string& passphrase,
                                          const std::uint8_t* salt, std::size_t salt_len,
                                          std::uint32_t iterations,
                                          std::uint8_t* out, std::size_t out_len) noexcept {
    if (iterations == 0) iterations = 1;
    std::size_t num_blocks = (out_len + 31) / 32;
    std::vector<std::uint8_t> block_salt;
    block_salt.reserve(salt_len + 4);

    std::uint8_t u_prev[32];
    std::uint8_t u_curr[32];
    std::uint8_t f_block[32];

    std::size_t out_pos = 0;
    for (std::uint32_t i = 1; i <= static_cast<std::uint32_t>(num_blocks); ++i) {
        block_salt.assign(salt, salt + salt_len);
        block_salt.push_back(static_cast<std::uint8_t>((i >> 24) & 0xFF));
        block_salt.push_back(static_cast<std::uint8_t>((i >> 16) & 0xFF));
        block_salt.push_back(static_cast<std::uint8_t>((i >> 8) & 0xFF));
        block_salt.push_back(static_cast<std::uint8_t>(i & 0xFF));

        // U_1 = HMAC(passphrase, Salt || INT(i))
        hmac_sha256(passphrase.data(), passphrase.size(),
                    block_salt.data(), block_salt.size(), u_prev);
        std::memcpy(f_block, u_prev, 32);

        // U_2..U_c
        for (std::uint32_t round = 2; round <= iterations; ++round) {
            hmac_sha256(passphrase.data(), passphrase.size(),
                        u_prev, 32, u_curr);
            for (std::size_t b = 0; b < 32; ++b) {
                f_block[b] ^= u_curr[b];
            }
            std::memcpy(u_prev, u_curr, 32);
        }

        std::size_t to_copy = (std::min)(static_cast<std::size_t>(32), out_len - out_pos);
        std::memcpy(out + out_pos, f_block, to_copy);
        out_pos += to_copy;
    }

    secure_zero_memory(u_prev, sizeof(u_prev));
    secure_zero_memory(u_curr, sizeof(u_curr));
    secure_zero_memory(f_block, sizeof(f_block));
}

bool DeveloperKeyStore::encrypt_with_passphrase(
    const std::vector<std::uint8_t>& plaintext,
    const std::string& passphrase,
    const std::vector<std::uint8_t>& salt,
    std::vector<std::uint8_t>& ciphertext,
    std::vector<std::uint8_t>& mac_tag) {

    if (passphrase.empty() || salt.size() < 16) return false;

    // Derive 64 bytes of key: 32B encryption key, 32B mac key
    std::uint8_t derived[64];
    pbkdf2_hmac_sha256(passphrase, salt.data(), salt.size(), 5000, derived, 64);

    const std::uint8_t* k_enc = derived;
    const std::uint8_t* k_mac = derived + 32;

    ciphertext.resize(plaintext.size());

    // Stream cipher via HMAC-SHA256 counter blocks
    std::size_t offset = 0;
    std::uint32_t counter = 0;
    std::uint8_t block[32];
    while (offset < plaintext.size()) {
        std::uint8_t ctr_buf[8];
        std::memcpy(ctr_buf, &counter, 4);
        std::memcpy(ctr_buf + 4, "STRM", 4);
        hmac_sha256(k_enc, 32, ctr_buf, 8, block);

        std::size_t chunk = (std::min)(static_cast<std::size_t>(32), plaintext.size() - offset);
        for (std::size_t b = 0; b < chunk; ++b) {
            ciphertext[offset + b] = plaintext[offset + b] ^ block[b];
        }
        offset += chunk;
        counter++;
    }

    // Authenticate: HMAC(k_mac, "TAG:" || salt || ciphertext)
    std::vector<std::uint8_t> tag_input;
    tag_input.reserve(4 + salt.size() + ciphertext.size());
    tag_input.insert(tag_input.end(), {'T', 'A', 'G', ':'});
    tag_input.insert(tag_input.end(), salt.begin(), salt.end());
    tag_input.insert(tag_input.end(), ciphertext.begin(), ciphertext.end());

    mac_tag.resize(32);
    hmac_sha256(k_mac, 32, tag_input.data(), tag_input.size(), mac_tag.data());

    secure_zero_memory(derived, sizeof(derived));
    secure_zero_memory(block, sizeof(block));
    return true;
}

bool DeveloperKeyStore::decrypt_with_passphrase(
    const std::vector<std::uint8_t>& ciphertext,
    const std::string& passphrase,
    const std::vector<std::uint8_t>& salt,
    const std::vector<std::uint8_t>& mac_tag,
    std::vector<std::uint8_t>& plaintext) {

    if (passphrase.empty() || salt.size() < 16 || mac_tag.size() != 32) return false;

    std::uint8_t derived[64];
    pbkdf2_hmac_sha256(passphrase, salt.data(), salt.size(), 5000, derived, 64);

    const std::uint8_t* k_enc = derived;
    const std::uint8_t* k_mac = derived + 32;

    // Verify tag
    std::vector<std::uint8_t> tag_input;
    tag_input.reserve(4 + salt.size() + ciphertext.size());
    tag_input.insert(tag_input.end(), {'T', 'A', 'G', ':'});
    tag_input.insert(tag_input.end(), salt.begin(), salt.end());
    tag_input.insert(tag_input.end(), ciphertext.begin(), ciphertext.end());

    std::uint8_t expected_tag[32];
    hmac_sha256(k_mac, 32, tag_input.data(), tag_input.size(), expected_tag);

    if (!constant_time_equals(expected_tag, mac_tag.data(), 32)) {
        secure_zero_memory(derived, sizeof(derived));
        return false;
    }

    // Decrypt
    plaintext.resize(ciphertext.size());
    std::size_t offset = 0;
    std::uint32_t counter = 0;
    std::uint8_t block[32];
    while (offset < ciphertext.size()) {
        std::uint8_t ctr_buf[8];
        std::memcpy(ctr_buf, &counter, 4);
        std::memcpy(ctr_buf + 4, "STRM", 4);
        hmac_sha256(k_enc, 32, ctr_buf, 8, block);

        std::size_t chunk = (std::min)(static_cast<std::size_t>(32), ciphertext.size() - offset);
        for (std::size_t b = 0; b < chunk; ++b) {
            plaintext[offset + b] = ciphertext[offset + b] ^ block[b];
        }
        offset += chunk;
        counter++;
    }

    secure_zero_memory(derived, sizeof(derived));
    secure_zero_memory(block, sizeof(block));
    return true;
}

bool DeveloperKeyStore::save_to_file(const std::string& file_path,
                                    const DeveloperIdentity& identity,
                                    KeystoreProtection protection,
                                    const std::string& passphrase) {
    if (!identity.is_valid()) return false;

    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> encrypted_seed;
    std::vector<std::uint8_t> tag;

    if (protection == KeystoreProtection::DPAPI) {
        if (!dpapi_protect(identity.private_key_seed, encrypted_seed)) {
            return false;
        }
    } else if (protection == KeystoreProtection::Passphrase) {
        if (passphrase.empty()) return false;
        salt.resize(16);
        SecureRandom::get_random_bytes(salt.data(), 16);
        if (!encrypt_with_passphrase(identity.private_key_seed, passphrase, salt, encrypted_seed, tag)) {
            return false;
        }
    } else if (protection == KeystoreProtection::DPAPIAndPassphrase) {
        if (passphrase.empty()) return false;
        salt.resize(16);
        SecureRandom::get_random_bytes(salt.data(), 16);
        std::vector<std::uint8_t> pass_enc;
        if (!encrypt_with_passphrase(identity.private_key_seed, passphrase, salt, pass_enc, tag)) {
            return false;
        }
        if (!dpapi_protect(pass_enc, encrypted_seed)) {
            return false;
        }
    } else {
        // None: plain seed hex (for test environments)
        encrypted_seed = identity.private_key_seed;
    }

    // Build keystore file content
    std::ostringstream oss;
    oss << "WIOSDEVKEY1\n";
    oss << "publisher_id=" << identity.publisher_id << "\n";
    oss << "org_name=" << identity.organization_name << "\n";
    oss << "key_id=" << identity.key_id << "\n";
    oss << "role=" << ((identity.role == PublisherRole::Platform) ? "Platform" : "Developer") << "\n";
    oss << "algorithm=Ed25519\n";
    oss << "created_at=" << identity.created_at << "\n";
    oss << "valid_from=" << identity.valid_from << "\n";
    oss << "valid_until=" << identity.valid_until << "\n";
    oss << "public_key_hex=" << bytes_to_hex(identity.public_key.data(), identity.public_key.size()) << "\n";
    oss << "protection=" << keystore_protection_to_string(protection) << "\n";

    if (!salt.empty()) {
        oss << "salt_hex=" << bytes_to_hex(salt.data(), salt.size()) << "\n";
    }
    if (!tag.empty()) {
        oss << "tag_hex=" << bytes_to_hex(tag.data(), tag.size()) << "\n";
    }
    oss << "encrypted_seed_hex=" << bytes_to_hex(encrypted_seed.data(), encrypted_seed.size()) << "\n";

    // Tamper detection checksum (SHA-256 over all preceding text)
    std::string text_so_far = oss.str();
    std::uint8_t checksum[32];
    crypto::Sha256 sha_save;
    sha_save.update(text_so_far.data(), text_so_far.size());
    sha_save.finalize(checksum);
    oss << "checksum_hex=" << bytes_to_hex(checksum, 32) << "\n";

    std::ofstream ofs(file_path, std::ios::binary);
    if (!ofs.is_open()) return false;
    std::string full_content = oss.str();
    ofs.write(full_content.data(), static_cast<std::streamsize>(full_content.size()));
    return ofs.good();
}

std::optional<DeveloperIdentity> DeveloperKeyStore::load_from_file(
    const std::string& file_path,
    const std::string& passphrase) {

    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) return std::nullopt;

    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    // Verify header magic
    if (content.rfind("WIOSDEVKEY1\n", 0) != 0) {
        return std::nullopt;
    }

    // Locate checksum_hex
    auto chk_pos = content.rfind("checksum_hex=");
    if (chk_pos == std::string::npos) return std::nullopt;
    auto nl_pos = content.find('\n', chk_pos);
    std::string chk_hex = content.substr(chk_pos + 13, (nl_pos == std::string::npos ? content.size() : nl_pos) - (chk_pos + 13));
    while (!chk_hex.empty() && (chk_hex.back() == '\r' || chk_hex.back() == '\n' || chk_hex.back() == ' ')) {
        chk_hex.pop_back();
    }

    std::string text_to_verify = content.substr(0, chk_pos);
    std::uint8_t expected_chk[32];
    crypto::Sha256 sha_load;
    sha_load.update(text_to_verify.data(), text_to_verify.size());
    sha_load.finalize(expected_chk);
    std::vector<std::uint8_t> actual_chk_bytes = hex_to_bytes(chk_hex);
    if (actual_chk_bytes.size() != 32 ||
        !constant_time_equals(expected_chk, actual_chk_bytes.data(), 32)) {
        return std::nullopt; // Checksum tamper failure
    }

    // Parse fields
    DeveloperIdentity id;
    KeystoreProtection protection = KeystoreProtection::None;
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> tag;
    std::vector<std::uint8_t> enc_seed;

    std::istringstream iss(text_to_verify);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty() || line == "WIOSDEVKEY1") continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);

        if (k == "publisher_id") id.publisher_id = v;
        else if (k == "org_name") id.organization_name = v;
        else if (k == "key_id") id.key_id = v;
        else if (k == "role") id.role = (v == "Platform") ? PublisherRole::Platform : PublisherRole::Developer;
        else if (k == "algorithm") id.algorithm = (v == "Ed25519") ? SignatureAlgorithm::Ed25519 : SignatureAlgorithm::HmacSha256;
        else if (k == "created_at") id.created_at = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "valid_from") id.valid_from = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "valid_until") id.valid_until = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "public_key_hex") id.public_key = hex_to_bytes(v);
        else if (k == "protection") protection = keystore_protection_from_string(v);
        else if (k == "salt_hex") salt = hex_to_bytes(v);
        else if (k == "tag_hex") tag = hex_to_bytes(v);
        else if (k == "encrypted_seed_hex") enc_seed = hex_to_bytes(v);
    }

    // Decrypt seed
    std::vector<std::uint8_t> decrypted_seed;
    if (protection == KeystoreProtection::DPAPI) {
        if (!dpapi_unprotect(enc_seed, decrypted_seed)) {
            return std::nullopt;
        }
    } else if (protection == KeystoreProtection::Passphrase) {
        if (!decrypt_with_passphrase(enc_seed, passphrase, salt, tag, decrypted_seed)) {
            return std::nullopt;
        }
    } else if (protection == KeystoreProtection::DPAPIAndPassphrase) {
        std::vector<std::uint8_t> pass_enc;
        if (!dpapi_unprotect(enc_seed, pass_enc)) {
            return std::nullopt;
        }
        if (!decrypt_with_passphrase(pass_enc, passphrase, salt, tag, decrypted_seed)) {
            return std::nullopt;
        }
    } else {
        decrypted_seed = enc_seed;
    }

    if (decrypted_seed.size() != 32) {
        secure_zero_memory(decrypted_seed.data(), decrypted_seed.size());
        return std::nullopt;
    }

    // Verify that the decrypted private seed matches the expected public key
    crypto::Ed25519Seed seed_check;
    std::memcpy(seed_check.data(), decrypted_seed.data(), 32);
    crypto::Ed25519PublicKey pub_check;
    crypto::Ed25519PrivateKey priv_check;
    crypto::Ed25519::create_keypair(pub_check, priv_check, seed_check);
    if (id.public_key.size() != 32 ||
        !constant_time_equals(pub_check.data(), id.public_key.data(), 32)) {
        secure_zero_memory(decrypted_seed.data(), decrypted_seed.size());
        return std::nullopt; // Key mismatch or corrupted seed
    }

    id.private_key_seed = std::move(decrypted_seed);
    return id;
}

} // namespace winios::security
