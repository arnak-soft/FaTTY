#include "app/version.hpp"

#include "core/paths.hpp"
#include "core/util.hpp"
#include "version_generated.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fatty {
namespace {

std::string cached;

std::string normalize(std::string text) {
  text = trim(text);
  if (text.size() > 1 && text[0] == 'v' && std::isdigit(static_cast<unsigned char>(text[1]))) {
    text.erase(text.begin());
  }
  static const std::regex hash_only(R"(^[0-9a-f]+(-dirty)?$)", std::regex::icase);
  if (std::regex_match(text, hash_only)) {
    return "0.0.0-g" + text;
  }
  return text;
}

std::string read_bundled() {
  auto try_path = [](const std::filesystem::path& p) -> std::string {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) return {};
    try {
      return trim(read_text_file(p));
    } catch (...) {
      return {};
    }
  };
  auto exe = exe_dir();
  if (auto t = try_path(exe / "_version.txt"); !t.empty()) return t;
  if (auto t = try_path(resource_root() / "_version.txt"); !t.empty()) return t;
  return {};
}

std::string run_git(const std::vector<std::string>& args) {
#ifdef _WIN32
  std::string cmd = "git -C \"" + std::string(FATTY_SOURCE_DIR) + "\"";
  for (const auto& a : args) {
    cmd += " " + a;
  }
  FILE* pipe = _popen(cmd.c_str(), "r");
  if (!pipe) return {};
  std::string out;
  char buf[512];
  while (fgets(buf, sizeof(buf), pipe)) out += buf;
  _pclose(pipe);
  return trim(out);
#else
  return {};
#endif
}

std::string git_version() {
  auto inside = run_git({"rev-parse", "--is-inside-work-tree"});
  if (inside != "true") return {};
  bool dirty = !run_git({"status", "--porcelain"}).empty();
  auto tags = run_git({"tag", "--list", "\"v[0-9]*\"", "--sort=-version:refname"});
  std::string chosen;
  if (!tags.empty()) {
    std::istringstream ss(tags);
    std::string tag;
    while (std::getline(ss, tag)) {
      tag = trim(tag);
      if (tag.empty()) continue;
      auto anc = run_git({"merge-base", "--is-ancestor", tag, "HEAD", "&&", "echo", "yes"});
      // merge-base doesn't print; check via another approach
      std::string cmd = "git -C \"" + std::string(FATTY_SOURCE_DIR) + "\" merge-base --is-ancestor " + tag + " HEAD";
      int rc = system((cmd + " >nul 2>nul").c_str());
      if (rc == 0) {
        chosen = tag;
        break;
      }
    }
  }
  std::string text;
  if (chosen.empty()) {
    auto short_hash = run_git({"rev-parse", "--short", "HEAD"});
    if (short_hash.empty()) return {};
    text = "0.0.0-g" + short_hash;
  } else {
    auto count = run_git({"rev-list", "--count", chosen + "..HEAD"});
    if (count.empty() || count == "0") {
      text = normalize(chosen);
    } else {
      auto short_hash = run_git({"rev-parse", "--short", "HEAD"});
      text = normalize(chosen) + "-" + count + "-g" + (short_hash.empty() ? "unknown" : short_hash);
    }
  }
  if (dirty) text += "-dirty";
  return text;
}

}  // namespace

std::string resolve_version() {
  if (cached.empty()) {
    auto bundled = read_bundled();
    if (!bundled.empty()) {
      cached = bundled;
    } else {
      auto git = git_version();
      cached = git.empty() ? std::string(FATTY_VERSION_STRING) : git;
    }
  }
  return cached;
}

std::tuple<int, int, int, int> version_tuple(const std::string& version) {
  std::string text = version.empty() ? resolve_version() : version;
  if (text.size() > 1 && text[0] == 'v' && std::isdigit(static_cast<unsigned char>(text[1]))) {
    text.erase(text.begin());
  }
  std::regex re(R"(^(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:-(\d+)-g)?)");
  std::smatch m;
  if (!std::regex_search(text, m, re)) {
    return {0, 0, 0, 0};
  }
  auto num = [](const std::ssub_match& s) { return s.matched ? std::stoi(s.str()) : 0; };
  return {num(m[1]), num(m[2]), num(m[3]), num(m[4])};
}

}  // namespace fatty
