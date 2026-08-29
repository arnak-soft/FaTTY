#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fatty {

inline constexpr const char* kAppName = "FaTTY";
inline constexpr const char* kLegacyAppName = "vps-runner";
inline constexpr const char* kGithubOwner = "arnak-soft";
inline constexpr const char* kGithubRepo = "FaTTY";
inline constexpr const char* kPuttyDownloadUrl =
    "https://www.chiark.greenend.org.uk/~sgtatham/putty/";

std::string trim(std::string_view text);
std::string to_lower(std::string_view text);
std::string clip(std::string text, std::size_t limit);
std::string new_uuid();
std::string copy_name(const std::string& base, const std::vector<std::string>& taken);
std::vector<std::string> prefer_order(const std::vector<std::string>& available,
                                      const std::vector<std::string>& preferred);

bool parse_int(std::string_view raw, int& out);
int clamp_int(int value, int lo, int hi);

void atomic_write_text(const std::filesystem::path& path, std::string_view text);
std::string read_text_file(const std::filesystem::path& path);

std::filesystem::path expand_user(const std::filesystem::path& path);
std::string posix_join(const std::string& cwd, const std::string& name);
std::string posix_parent(const std::string& path);

std::string b64_encode(const unsigned char* data, std::size_t len);
std::vector<unsigned char> b64_decode(std::string_view text);

void secure_clear(std::string& s);
void secure_clear(std::vector<unsigned char>& v);

}  // namespace fatty
