#include "net/updates.hpp"

#include "app/version.hpp"
#include "core/util.hpp"

#include <nlohmann/json.hpp>
#include <regex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace fatty {
using json = nlohmann::json;

namespace {

constexpr const char* kOwner = "arnak-soft";
constexpr const char* kRepo = "FaTTY";

std::string normalize_version(std::string text) {
  text = trim(text);
  if (text.size() > 1 && text[0] == 'v' && std::isdigit(static_cast<unsigned char>(text[1]))) {
    text.erase(text.begin());
  }
  return text;
}

std::tuple<int, int, int> version_key(const std::string& raw) {
  auto text = normalize_version(raw);
  std::regex re(R"(^v?(\d+)(?:\.(\d+))?(?:\.(\d+))?)", std::regex::icase);
  std::smatch m;
  if (!std::regex_search(text, m, re)) return {0, 0, 0};
  auto n = [](const std::ssub_match& s) { return s.matched ? std::stoi(s.str()) : 0; };
  return {n(m[1]), n(m[2]), n(m[3])};
}

bool is_newer(const std::string& remote, const std::string& local) {
  return version_key(remote) > version_key(local);
}

#ifdef _WIN32
std::string winhttp_get(const std::wstring& host, const std::wstring& path) {
  HINTERNET session = WinHttpOpen(L"FaTTY", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) throw UpdateError("Нет связи с GitHub");
  WinHttpSetTimeouts(session, 12000, 12000, 12000, 12000);
  HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!connect) {
    WinHttpCloseHandle(session);
    throw UpdateError("Нет связи с GitHub");
  }
  HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!request) {
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    throw UpdateError("Нет связи с GitHub");
  }
  std::wstring headers =
      L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\nUser-Agent: FaTTY\r\n";
  BOOL ok = WinHttpSendRequest(request, headers.c_str(), (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr);
  if (!ok) {
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    throw UpdateError("Нет связи с GitHub");
  }
  DWORD status = 0;
  DWORD size = sizeof(status);
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  std::string body;
  DWORD avail = 0;
  while (WinHttpQueryDataAvailable(request, &avail) && avail) {
    std::string chunk(avail, '\0');
    DWORD read = 0;
    WinHttpReadData(request, chunk.data(), avail, &read);
    body.append(chunk.data(), read);
  }
  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  if (status == 404) return {};
  if (status != 200) throw UpdateError("GitHub ответил HTTP " + std::to_string(status));
  return body;
}
#endif

std::optional<std::string> pick_asset_url(const json& assets) {
  if (!assets.is_array()) return std::nullopt;
  std::vector<std::pair<std::string, std::string>> names;
  for (const auto& item : assets) {
    auto name = to_lower(item.value("name", ""));
    auto url = item.value("browser_download_url", "");
    if (!name.empty() && !url.empty()) names.emplace_back(name, url);
  }
  for (const char* needle : {"setup.exe", "setup", "onefile.exe", "onefile", ".exe"}) {
    for (const auto& [name, url] : names) {
      if (name.find(needle) != std::string::npos) return url;
    }
  }
  if (!names.empty()) return names[0].second;
  return std::nullopt;
}

UpdateCheckResult from_release(const json& data, const std::string& current) {
  auto tag = normalize_version(data.value("tag_name", data.value("name", "")));
  if (tag.empty()) return {"none", current, std::nullopt, std::nullopt, std::nullopt};
  std::string page = data.value("html_url", "");
  if (page.empty()) page = "https://github.com/arnak-soft/FaTTY/releases/tag/v" + tag;
  auto download = pick_asset_url(data.value("assets", json::array()));
  if (is_newer(tag, current)) {
    return {"update", current, tag, page, download ? download : page};
  }
  return {"current", current, tag, page, std::nullopt};
}

UpdateCheckResult from_tags(const json& data, const std::string& current) {
  std::string page = "https://github.com/arnak-soft/FaTTY/releases";
  if (!data.is_array() || data.empty()) return {"none", current, std::nullopt, page, std::nullopt};
  std::string best;
  for (const auto& item : data) {
    auto name = normalize_version(item.value("name", ""));
    if (name.empty() || !std::isdigit(static_cast<unsigned char>(name[0]))) continue;
    if (best.empty() || is_newer(name, best)) best = name;
  }
  if (best.empty()) return {"none", current, std::nullopt, page, std::nullopt};
  page = "https://github.com/arnak-soft/FaTTY/releases/tag/v" + best;
  if (is_newer(best, current)) return {"update", current, best, page, page};
  return {"current", current, best, page, std::nullopt};
}

}  // namespace

UpdateCheckResult check_for_updates(const std::string& current_in) {
  auto local = normalize_version(current_in.empty() ? resolve_version() : current_in);
#ifdef _WIN32
  auto body = winhttp_get(L"api.github.com", L"/repos/arnak-soft/FaTTY/releases/latest");
  if (!body.empty()) {
    auto parsed = json::parse(body, nullptr, false);
    if (parsed.is_object()) return from_release(parsed, local);
  }
  body = winhttp_get(L"api.github.com", L"/repos/arnak-soft/FaTTY/tags");
  auto parsed = json::parse(body.empty() ? "[]" : body, nullptr, false);
  return from_tags(parsed, local);
#else
  return {"none", local, std::nullopt, std::nullopt, std::nullopt};
#endif
}

}  // namespace fatty
