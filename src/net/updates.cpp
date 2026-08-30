#include "net/updates.hpp"

#include "app/version.hpp"
#include "core/util.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <vector>

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

bool looks_like_asset_url(const std::string& url) {
  auto u = to_lower(url);
  return u.find("/releases/download/") != std::string::npos ||
         u.find("/latest/download/") != std::string::npos;
}

std::string setup_asset_url(const std::string& version) {
  auto ver = normalize_version(version);
  if (ver.empty()) return {};
  return github_repo_url("/releases/download/v" + ver + "/FaTTY." + ver + ".Setup.exe");
}

std::optional<std::string> pick_asset_url(const json& assets) {
  if (!assets.is_array()) return std::nullopt;
  std::vector<std::pair<std::string, std::string>> names;
  for (const auto& item : assets) {
    auto url = item.value("browser_download_url", "");
    if (url.empty()) continue;
    // GitHub подменяет пробелы в name на точки («FaTTY.1.6.6.Setup.exe»),
    // исходное имя остаётся в label («FaTTY 1.6.6 Setup.exe»).
    auto hay = to_lower(item.value("name", "")) + " " + to_lower(item.value("label", ""));
    if (hay.find_first_not_of(' ') == std::string::npos) continue;
    names.emplace_back(hay, url);
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
  if (!download) download = setup_asset_url(tag);
  if (is_newer(tag, current)) {
    return {"update", current, tag, page, download};
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
  auto download = setup_asset_url(best);
  if (is_newer(best, current)) return {"update", current, best, page, download};
  return {"current", current, best, page, std::nullopt};
}

#ifdef _WIN32
std::wstring utf8_wide(const std::string& text) {
  if (text.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
  return out;
}

void winhttp_download_once(DWORD access_type, const std::string& url, const std::filesystem::path& dest) {
  auto wurl = utf8_wide(url);
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256]{};
  wchar_t path[4096]{};
  wchar_t extra[2048]{};
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 4096;
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 2048;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) throw_github_transport(GetLastError());
  if (uc.nScheme != INTERNET_SCHEME_HTTPS) throw UpdateError("Ссылка на установщик не HTTPS");

  std::wstring full_path(path, uc.dwUrlPathLength);
  if (uc.dwExtraInfoLength) full_path.append(extra, uc.dwExtraInfoLength);

  WinHttpHandle session{WinHttpOpen(L"FaTTY", access_type, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session) throw_github_transport(GetLastError());
  enable_secure_session(session.h);
  WinHttpSetTimeouts(session.h, 15000, 15000, 15000, 120000);
  WinHttpHandle connect{WinHttpConnect(session.h, host, INTERNET_DEFAULT_HTTPS_PORT, 0)};
  if (!connect) throw_github_transport(GetLastError());
  WinHttpHandle request{WinHttpOpenRequest(connect.h, L"GET", full_path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
  if (!request) throw_github_transport(GetLastError());
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
  std::wstring headers = L"Accept: */*\r\nUser-Agent: FaTTY\r\n";
  if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    throw_github_transport(GetLastError());
  }
  DWORD status = 0;
  DWORD size = sizeof(status);
  WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  if (status != 200) throw UpdateError("GitHub ответил HTTP " + std::to_string(status));

  auto part = dest;
  part += ".part";
  std::ofstream out(part, std::ios::binary | std::ios::trunc);
  if (!out) throw UpdateError("Не удалось записать файл установщика");
  DWORD avail = 0;
  while (WinHttpQueryDataAvailable(request.h, &avail) && avail) {
    std::string chunk(avail, '\0');
    DWORD read = 0;
    WinHttpReadData(request.h, chunk.data(), avail, &read);
    out.write(chunk.data(), static_cast<std::streamsize>(read));
    if (!out) {
      out.close();
      std::filesystem::remove(part);
      throw UpdateError("Не удалось записать файл установщика");
    }
  }
  out.close();

  std::ifstream in(part, std::ios::binary);
  char mz[2]{};
  in.read(mz, 2);
  in.close();
  std::error_code ec;
  auto bytes = std::filesystem::file_size(part, ec);
  if (ec || bytes < 256 * 1024 || mz[0] != 'M' || mz[1] != 'Z') {
    std::filesystem::remove(part, ec);
    throw UpdateError("GitHub отдал не установщик");
  }
  std::filesystem::remove(dest, ec);
  std::filesystem::rename(part, dest, ec);
  if (ec) throw UpdateError("Не удалось сохранить установщик");
}

void winhttp_download(const std::string& url, const std::filesystem::path& dest) {
  try {
    winhttp_download_once(WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, url, dest);
    return;
  } catch (const UpdateError& e) {
    std::string what = e.what();
    if (what.find("HTTP ") != std::string::npos) throw;
  }
  winhttp_download_once(WINHTTP_ACCESS_TYPE_NO_PROXY, url, dest);
}
#endif

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

std::optional<std::string> pick_github_setup_url(std::string_view assets_json) {
  auto parsed = json::parse(std::string(assets_json), nullptr, false);
  if (parsed.is_discarded()) return std::nullopt;
  if (parsed.is_object()) return pick_asset_url(parsed.value("assets", json::array()));
  return pick_asset_url(parsed);
}

std::vector<std::string> installer_download_urls(const std::string& version,
                                                 const std::optional<std::string>& preferred) {
  std::vector<std::string> urls;
  auto add = [&](const std::string& u) {
    if (u.empty()) return;
    if (std::find(urls.begin(), urls.end(), u) != urls.end()) return;
    urls.push_back(u);
  };
  if (preferred && looks_like_asset_url(*preferred)) add(*preferred);
  auto ver = normalize_version(version);
  if (!ver.empty()) {
    add(setup_asset_url(ver));
    add(github_repo_url("/releases/download/v" + ver + "/FaTTY%20" + ver + "%20Setup.exe"));
    add(github_repo_url("/releases/latest/download/FaTTY." + ver + ".Setup.exe"));
  }
  return urls;
}

void download_installer(const std::string& version, const std::optional<std::string>& preferred_url,
                       const std::filesystem::path& dest) {
#ifdef _WIN32
  auto urls = installer_download_urls(version, preferred_url);
  if (urls.empty()) throw UpdateError("Нет ссылки на установщик");
  std::string last = "Не удалось скачать установщик";
  for (const auto& url : urls) {
    try {
      winhttp_download(url, dest);
      return;
    } catch (const UpdateError& e) {
      last = e.what();
    }
  }
  throw UpdateError(last);
#else
  (void)version;
  (void)preferred_url;
  (void)dest;
  throw UpdateError("Скачивание установщика только на Windows");
#endif
}

}  // namespace fatty
