#include "core/util.hpp"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <rpc.h>
#endif

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace fatty {
namespace {

std::filesystem::path temp_beside(const std::filesystem::path& path, const std::string& prefix) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 0x7fffffff);
  auto dir = path.parent_path();
  if (dir.empty()) {
    dir = std::filesystem::current_path();
  }
  return dir / (prefix + std::to_string(dist(gen)) + ".tmp");
}

}  // namespace

std::string trim(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::string to_lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::string clip(std::string text, std::size_t limit) {
  if (text.size() <= limit) {
    return text;
  }
  if (limit == 0) {
    return "";
  }
  text.resize(limit - 1);
  text.push_back(static_cast<char>(0xE2));
  text.push_back(static_cast<char>(0x80));
  text.push_back(static_cast<char>(0xA6));  // …
  return text;
}

std::string new_uuid() {
#ifdef _WIN32
  UUID uuid{};
  if (UuidCreate(&uuid) != RPC_S_OK) {
    throw std::runtime_error("UuidCreate failed");
  }
  RPC_CSTR str = nullptr;
  if (UuidToStringA(&uuid, &str) != RPC_S_OK) {
    throw std::runtime_error("UuidToString failed");
  }
  std::string out(reinterpret_cast<char*>(str));
  RpcStringFreeA(&str);
  return out;
#else
  unsigned char bytes[16];
  RAND_bytes(bytes, 16);
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
  char buf[37];
  std::snprintf(
      buf,
      sizeof(buf),
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      bytes[0],
      bytes[1],
      bytes[2],
      bytes[3],
      bytes[4],
      bytes[5],
      bytes[6],
      bytes[7],
      bytes[8],
      bytes[9],
      bytes[10],
      bytes[11],
      bytes[12],
      bytes[13],
      bytes[14],
      bytes[15]);
  return buf;
#endif
}

std::string copy_name(const std::string& base, const std::vector<std::string>& taken) {
  auto has = [&](const std::string& name) {
    return std::find(taken.begin(), taken.end(), name) != taken.end();
  };
  std::string stem = base + " (копия)";
  if (!has(stem)) {
    return stem;
  }
  for (int n = 2;; ++n) {
    std::string candidate = base + " (копия " + std::to_string(n) + ")";
    if (!has(candidate)) {
      return candidate;
    }
  }
}

bool parse_int(std::string_view raw, int& out) {
  try {
    std::size_t idx = 0;
    std::string s(raw);
    int value = std::stoi(s, &idx, 10);
    if (idx != s.size()) {
      return false;
    }
    out = value;
    return true;
  } catch (...) {
    return false;
  }
}

int clamp_int(int value, int lo, int hi) {
  return std::max(lo, std::min(hi, value));
}

void atomic_write_text(const std::filesystem::path& path, std::string_view text) {
  auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  auto tmp = temp_beside(path, path.stem().string() + "-");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw std::runtime_error("Не удалось создать временный файл: " + tmp.string());
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!text.empty() && text.back() != '\n') {
      out.put('\n');
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(tmp);
    throw std::runtime_error("Не удалось сохранить файл: " + path.string());
  }
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("Не удалось прочитать файл: " + path.string());
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::filesystem::path expand_user(const std::filesystem::path& path) {
  auto text = path.u8string();
  std::string s(text.begin(), text.end());
  if (!s.empty() && s[0] == '~') {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (home && home[0]) {
      if (s.size() == 1) {
        return std::filesystem::path(home);
      }
      if (s.size() > 1 && (s[1] == '/' || s[1] == '\\')) {
        return std::filesystem::path(home) / std::filesystem::path(s.substr(2));
      }
    }
  }
  return path;
}

std::string posix_join(const std::string& cwd, const std::string& name) {
  if (name.empty() || name == ".") {
    return cwd.empty() ? "." : cwd;
  }
  if (name[0] == '/') {
    return name;
  }
  if (cwd.empty() || cwd == ".") {
    return name;
  }
  if (cwd == "/") {
    return "/" + name;
  }
  if (cwd.back() == '/') {
    return cwd + name;
  }
  return cwd + "/" + name;
}

std::string posix_parent(const std::string& path) {
  std::string text = path;
  while (text.size() > 1 && text.back() == '/') {
    text.pop_back();
  }
  if (text.empty() || text == "/") {
    return "/";
  }
  auto pos = text.rfind('/');
  if (pos == std::string::npos) {
    return "/";
  }
  if (pos == 0) {
    return "/";
  }
  return text.substr(0, pos);
}

std::string b64_encode(const unsigned char* data, std::size_t len) {
  if (len == 0) {
    return "";
  }
  std::string out(((len + 2) / 3) * 4, '\0');
  const int n = EVP_EncodeBlock(
      reinterpret_cast<unsigned char*>(out.data()), data, static_cast<int>(len));
  if (n < 0) {
    throw std::runtime_error("base64 encode failed");
  }
  out.resize(static_cast<std::size_t>(n));
  return out;
}

std::vector<unsigned char> b64_decode(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  std::string padded(text);
  while (padded.size() % 4 != 0) {
    padded.push_back('=');
  }
  std::vector<unsigned char> out((padded.size() / 4) * 3);
  const int n = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char*>(padded.data()),
                                static_cast<int>(padded.size()));
  if (n < 0) {
    throw std::runtime_error("base64 decode failed");
  }
  std::size_t len = static_cast<std::size_t>(n);
  if (!padded.empty() && padded[padded.size() - 1] == '=') {
    --len;
  }
  if (padded.size() >= 2 && padded[padded.size() - 2] == '=') {
    --len;
  }
  out.resize(len);
  return out;
}

void secure_clear(std::string& s) {
  if (!s.empty()) {
    OPENSSL_cleanse(s.data(), s.size());
    s.clear();
  }
}

void secure_clear(std::vector<unsigned char>& v) {
  if (!v.empty()) {
    OPENSSL_cleanse(v.data(), v.size());
    v.clear();
  }
}

}  // namespace fatty
