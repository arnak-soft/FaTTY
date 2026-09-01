#include "core/paths.hpp"

#include "core/util.hpp"

#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace fatty {
namespace {

std::filesystem::path resolve_app_dir() {
  std::filesystem::path root;
#ifdef _WIN32
  const char* appdata = std::getenv("APPDATA");
  if (appdata && appdata[0]) {
    root = std::filesystem::path(appdata);
  } else {
    const char* home = std::getenv("USERPROFILE");
    root = home ? std::filesystem::path(home) : std::filesystem::current_path();
  }
#else
  const char* home = std::getenv("HOME");
  root = home ? std::filesystem::path(home) : std::filesystem::current_path();
#endif
  auto current = root / kAppName;
  auto legacy = root / kLegacyAppName;
  if (std::filesystem::exists(current) || !std::filesystem::exists(legacy)) {
    return current;
  }
  std::error_code ec;
  std::filesystem::rename(legacy, current, ec);
  if (ec) {
    return legacy;
  }
  return current;
}

}  // namespace

const std::filesystem::path& app_dir() {
  static const auto path = resolve_app_dir();
  return path;
}

const std::filesystem::path& config_path() {
  static const auto path = app_dir() / "config.json";
  return path;
}

const std::filesystem::path& known_hosts_path() {
  static const auto path = app_dir() / "known_hosts";
  return path;
}

const std::filesystem::path& journal_path() {
  static const auto path = app_dir() / "journal.jsonl";
  return path;
}

const std::filesystem::path& lockout_path() {
  static const auto path = app_dir() / "auth-lockout.json";
  return path;
}

const std::filesystem::path& error_log_path() {
  static const auto path = app_dir() / "fatty.log";
  return path;
}

const std::filesystem::path& putty_keys_dir() {
  static const auto path = app_dir() / "putty-keys";
  return path;
}

std::filesystem::path exe_dir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return std::filesystem::current_path();
  }
  return std::filesystem::path(buf).parent_path();
#else
  return std::filesystem::current_path();
#endif
}

std::filesystem::path resource_root() {
  auto exe = exe_dir();
  if (std::filesystem::exists(exe / "assets" / "app.ico") ||
      std::filesystem::exists(exe / "assets" / "app.png")) {
    return exe;
  }
  if (std::filesystem::exists(exe / "app.ico")) {
    return exe;
  }
  auto up = exe.parent_path();
  if (std::filesystem::exists(up / "assets" / "app.ico")) {
    return up;
  }
  up = up.parent_path();
  if (std::filesystem::exists(up / "assets" / "app.ico")) {
    return up;
  }
#ifdef FATTY_SOURCE_DIR
  return std::filesystem::path(FATTY_SOURCE_DIR);
#else
  return exe;
#endif
}

void open_directory(const std::filesystem::path& path) {
  std::filesystem::create_directories(path);
#ifdef _WIN32
  ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

void open_path(const std::filesystem::path& path) {
#ifdef _WIN32
  ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

void open_url(const std::string& url) {
#ifdef _WIN32
  ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

}  // namespace fatty
