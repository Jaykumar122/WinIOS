#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <mutex>
#include <optional>

namespace winios::kernel {

enum class PrefType : std::uint8_t {
    Int,
    Float,
    String,
    Bool,
    Bytes
};

struct PrefValue {
    PrefType type{PrefType::Int};
    std::variant<std::int64_t, double, std::string, bool, std::vector<std::uint8_t>> value;

    static PrefValue make_int(std::int64_t v) { return {PrefType::Int, v}; }
    static PrefValue make_float(double v) { return {PrefType::Float, v}; }
    static PrefValue make_string(const std::string& v) { return {PrefType::String, v}; }
    static PrefValue make_bool(bool v) { return {PrefType::Bool, v}; }
    static PrefValue make_bytes(const std::vector<std::uint8_t>& v) { return {PrefType::Bytes, v}; }
};

class AppPreferencesManager {
public:
    static AppPreferencesManager& instance() noexcept;

    void reset() noexcept;

    // Setting values
    bool set_int(const std::string& app_id, const std::string& key, std::int64_t val);
    bool set_float(const std::string& app_id, const std::string& key, double val);
    bool set_string(const std::string& app_id, const std::string& key, const std::string& val);
    bool set_bool(const std::string& app_id, const std::string& key, bool val);
    bool set_bytes(const std::string& app_id, const std::string& key, const std::vector<std::uint8_t>& val);

    // Getting values
    [[nodiscard]] std::optional<std::int64_t> get_int(const std::string& app_id, const std::string& key) const;
    [[nodiscard]] std::optional<double> get_float(const std::string& app_id, const std::string& key) const;
    [[nodiscard]] std::optional<std::string> get_string(const std::string& app_id, const std::string& key) const;
    [[nodiscard]] std::optional<bool> get_bool(const std::string& app_id, const std::string& key) const;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> get_bytes(const std::string& app_id, const std::string& key) const;

    bool has_key(const std::string& app_id, const std::string& key) const;
    bool remove_key(const std::string& app_id, const std::string& key);
    void clear_app_preferences(const std::string& app_id);

    [[nodiscard]] std::size_t app_key_count(const std::string& app_id) const;

    // Snapshot serialization & deserialization
    void serialize(std::vector<std::uint8_t>& dst) const;
    bool deserialize(const std::uint8_t*& src, std::size_t& remaining);

private:
    AppPreferencesManager() = default;

    mutable std::mutex mutex_;
    // app_id -> (key -> PrefValue)
    std::unordered_map<std::string, std::unordered_map<std::string, PrefValue>> store_;
};

} // namespace winios::kernel
