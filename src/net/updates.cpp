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

std::string github_repo_url(const std::string& suffix) {
  return std::string("https://github.com/") + kGithubOwner + "/" + kGithubRepo + suffix;
}

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
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif
#ifndef WINHTTP_OPTION_IPV6_FAST_FALLBACK
#define WINHTTP_OPTION_IPV6_FAST_FALLBACK 110
#endif

struct WinHttpHandle {
  HINTERNET h = nullptr;
  WinHttpHandle() = default;
  explicit WinHttpHandle(HINTERNET handle) : h(handle) {}
  ~WinHttpHandle() { reset(); }
  WinHttpHandle(const WinHttpHandle&) = delete;
  WinHttpHandle& operator=(const WinHttpHandle&) = delete;
  WinHttpHandle(WinHttpHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
  WinHttpHandle& operator=(WinHttpHandle&& o) noexcept {
    if (this != &o) {
      reset();
      h = o.h;
      o.h = nullptr;
    }
    return *this;
  }
  void reset() {
    if (h) {
      WinHttpCloseHandle(h);
      h = nullptr;
    }
  }
  explicit operator bool() const { return h != nullptr; }
};

[[noreturn]] void throw_github_transport(DWORD err) {
  std::string msg = "Нет связи с GitHub";
  switch (err) {
    case ERROR_WINHTTP_TIMEOUT:
      msg += ": истекло время ожидания";
      break;
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
      msg += ": не удалось найти api.github.com";
      break;
    case ERROR_WINHTTP_CANNOT_CONNECT:
      msg += ": соединение отклонено";
      break;
    case ERROR_WINHTTP_CONNECTION_ERROR:
      msg += ": соединение разорвано";
      break;
    case ERROR_WINHTTP_SECURE_FAILURE:
      msg += ": ошибка HTTPS (TLS или сертификат)";
      break;
    case ERROR_WINHTTP_LOGIN_FAILURE:
      msg += ": прокси требует авторизацию";
      break;
    default:
      if (err) msg += " (код " + std::to_string(err) + ")";
      break;
  }
  throw UpdateError(msg);
}

void enable_secure_session(HINTERNET session) {
  DWORD tls13 = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
  if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls13, sizeof(tls13))) {
    DWORD tls12 = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls12, sizeof(tls12));
  }
  DWORD ipv6 = TRUE;
  WinHttpSetOption(session, WINHTTP_OPTION_IPV6_FAST_FALLBACK, &ipv6, sizeof(ipv6));
  WinHttpSetTimeouts(session, 15000, 15000, 15000, 15000);
}

std::wstring github_api_path(const wchar_t* suffix) {
  std::wstring path = L"/repos/";
  path.append(kGithubOwner, kGithubOwner + std::char_traits<char>::length(kGithubOwner));
  path.push_back(L'/');
  path.append(kGithubRepo, kGithubRepo + std::char_traits<char>::length(kGithubRepo));
  path += suffix;
  return path;
}

std::string winhttp_get_once(DWORD access_type, const std::wstring& host, const std::wstring& path) {
  WinHttpHandle session{WinHttpOpen(L"FaTTY", access_type, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session) throw_github_transport(GetLastError());
  enable_secure_session(session.h);
  WinHttpHandle connect{WinHttpConnect(session.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
  if (!connect) throw_github_transport(GetLastError());
  WinHttpHandle request{WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
  if (!request) throw_github_transport(GetLastError());
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
  std::wstring headers =
      L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\nUser-Agent: FaTTY\r\n";
  if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    throw_github_transport(GetLastError());
  }
  DWORD status = 0;
  DWORD size = sizeof(status);
  WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  std::string body;
  DWORD avail = 0;
  while (WinHttpQueryDataAvailable(request.h, &avail) && avail) {
    std::string chunk(avail, '\0');
    DWORD read = 0;
    WinHttpReadData(request.h, chunk.data(), avail, &read);
    body.append(chunk.data(), read);
  }
  if (status == 404) return {};
  if (status == 403) throw UpdateError("GitHub отклонил запрос (HTTP 403). Попробуйте позже.");
  if (status != 200) throw UpdateError("GitHub ответил HTTP " + std::to_string(status));
  return body;
}

std::string winhttp_get(const std::wstring& host, const std::wstring& path) {
  try {
    return winhttp_get_once(WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, host, path);
  } catch (const UpdateError& e) {
    std::string what = e.what();
    if (what.find("HTTP ") != std::string::npos) throw;
  }
  return winhttp_get_once(WINHTTP_ACCESS_TYPE_NO_PROXY, host, path);
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
  if (page.empty()) page = github_repo_url("/releases/tag/v" + tag);
  auto download = pick_asset_url(data.value("assets", json::array()));
  if (is_newer(tag, current)) {
    return {"update", current, tag, page, download ? download : page};
  }
  return {"current", current, tag, page, std::nullopt};
}

UpdateCheckResult from_tags(const json& data, const std::string& current) {
  std::string page = github_repo_url("/releases");
  if (!data.is_array() || data.empty()) return {"none", current, std::nullopt, page, std::nullopt};
  std::string best;
  for (const auto& item : data) {
    auto name = normalize_version(item.value("name", ""));
    if (name.empty() || !std::isdigit(static_cast<unsigned char>(name[0]))) continue;
    if (best.empty() || is_newer(name, best)) best = name;
  }
  if (best.empty()) return {"none", current, std::nullopt, page, std::nullopt};
  page = github_repo_url("/releases/tag/v" + best);
  if (is_newer(best, current)) return {"update", current, best, page, page};
  return {"current", current, best, page, std::nullopt};
}

}  // namespace

UpdateCheckResult check_for_updates(const std::string& current_in) {
  auto local = normalize_version(current_in.empty() ? resolve_version() : current_in);
#ifdef _WIN32
  auto body = winhttp_get(L"api.github.com", github_api_path(L"/releases/latest"));
  if (!body.empty()) {
    auto parsed = json::parse(body, nullptr, false);
    if (parsed.is_object()) return from_release(parsed, local);
  }
  body = winhttp_get(L"api.github.com", github_api_path(L"/tags"));
  auto parsed = json::parse(body.empty() ? "[]" : body, nullptr, false);
  return from_tags(parsed, local);
#else
  return {"none", local, std::nullopt, std::nullopt, std::nullopt};
#endif
}

}  // namespace fatty
