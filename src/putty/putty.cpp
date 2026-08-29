#include "putty/putty.hpp"

#include "core/paths.hpp"
#include "core/placeholders.hpp"
#include "core/util.hpp"
#include "putty/ppk.hpp"

#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <openssl/sha.h>

namespace fatty {
namespace {

std::string win_cmd_quote(const std::string& arg) {
  if (arg.empty()) return "\"\"";
  if (arg.find_first_of(" \t\"&<>|^()%") == std::string::npos) return arg;
  std::string out = "\"";
  for (char ch : arg) {
    if (ch == '"') out += "\"\"";
    else out += ch;
  }
  out += "\"";
  return out;
}

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& text) {
  if (text.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
  return out;
}

void launch_detached(const std::filesystem::path& exe, const std::string& args_tail, const char* fail_message) {
  std::wstring cmd = L"\"" + exe.wstring() + L"\"";
  if (!args_tail.empty()) {
    cmd += L" " + utf8_to_wide(args_tail);
  }
  STARTUPINFOW si{};
  PROCESS_INFORMATION pi{};
  si.cb = sizeof(si);
  std::filesystem::path cwd = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : ".";
  std::wstring cwd_w = cwd.wstring();
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd_w.c_str(), &si, &pi)) {
    throw PuttyLaunchError(fail_message);
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
}
#endif

std::optional<std::filesystem::path> which_exe(const std::string& name) {
#ifdef _WIN32
  char buf[MAX_PATH];
  DWORD n = SearchPathA(nullptr, name.c_str(), ".exe", MAX_PATH, buf, nullptr);
  if (n > 0 && n < MAX_PATH) return std::filesystem::path(buf);
#endif
  return std::nullopt;
}

std::filesystem::path putty_key_cache_path(const std::filesystem::path& source) {
  auto st = std::filesystem::last_write_time(source);
  auto size = std::filesystem::file_size(source);
  std::string raw = source.lexically_normal().string() + ":" +
                    std::to_string(st.time_since_epoch().count()) + ":" + std::to_string(size);
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(raw.data()), raw.size(), digest);
  static const char* hex = "0123456789abcdef";
  std::string h;
  for (int i = 0; i < 16; ++i) {
    h.push_back(hex[(digest[i] >> 4) & 0xf]);
    h.push_back(hex[digest[i] & 0xf]);
  }
  auto dir = putty_keys_dir();
  std::filesystem::create_directories(dir);
  return dir / (h + ".ppk");
}

std::filesystem::path convert_openssh_to_ppk(const std::filesystem::path& source) {
  auto dest = putty_key_cache_path(source);
  if (std::filesystem::is_regular_file(dest) &&
      std::filesystem::last_write_time(dest) >= std::filesystem::last_write_time(source)) {
    try {
      auto head = read_text_file(dest).substr(0, 40);
      if (head.rfind("PuTTY-User-Key-File-", 0) == 0) return dest;
    } catch (...) {
    }
  }
  try {
    return write_openssh_as_ppk(source, dest);
  } catch (const PPKError& exc) {
    throw SSHError(exc.what());
  }
}

std::filesystem::path resolve_putty_key(const std::string& key_path) {
  auto key = expand_user(key_path);
  if (!std::filesystem::is_regular_file(key)) {
    throw SSHError("SSH-ключ не найден: " + key.string());
  }
  if (to_lower(key.extension().string()) == ".ppk") return key;
  return convert_openssh_to_ppk(key);
}

std::filesystem::path putty_password_file(const std::string& password) {
  std::filesystem::create_directories(app_dir());
  auto path = app_dir() / ("putty-pw-" + new_uuid().substr(0, 8) + ".tmp");
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << password << "\n";
  return path;
}

void schedule_unlink(const std::filesystem::path& path) {
  std::thread([path] {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }).detach();
}

std::optional<std::filesystem::path> try_resolve_key(const Server& server) {
  auto key_path = trim(server.key_path);
  if (key_path.empty()) return std::nullopt;
  try {
    return resolve_putty_key(key_path);
  } catch (const SSHError&) {
    if (trim(server.password).empty()) throw;
    return std::nullopt;
  }
}

}  // namespace

std::optional<std::filesystem::path> find_ssh_executable(const std::string& custom_path) {
  if (!custom_path.empty()) {
    auto c = expand_user(custom_path);
    if (std::filesystem::is_regular_file(c)) return c;
  }
  if (auto found = which_exe("ssh")) return found;
#ifdef _WIN32
  const char* windir = std::getenv("WINDIR");
  std::filesystem::path win = windir && windir[0] ? windir : "C:\\Windows";
  for (auto rel : {std::filesystem::path("System32") / "OpenSSH" / "ssh.exe",
                   std::filesystem::path("Sysnative") / "OpenSSH" / "ssh.exe"}) {
    auto c = win / rel;
    if (std::filesystem::is_regular_file(c)) return c;
  }
#endif
  return std::nullopt;
}

std::optional<std::filesystem::path> find_putty_executable(const std::string& custom_path) {
  if (!custom_path.empty()) {
    auto c = expand_user(custom_path);
    if (std::filesystem::is_regular_file(c)) return c;
  }
  if (auto found = which_exe("putty")) return found;
#ifdef _WIN32
  const char* pf = std::getenv("ProgramFiles");
  const char* pf86 = std::getenv("ProgramFiles(x86)");
  const char* local = std::getenv("LocalAppData");
  std::vector<std::filesystem::path> dirs;
  if (pf) dirs.push_back(std::filesystem::path(pf) / "PuTTY");
  if (pf86) dirs.push_back(std::filesystem::path(pf86) / "PuTTY");
  if (local) dirs.push_back(std::filesystem::path(local) / "Programs" / "PuTTY");
  for (const auto& dir : dirs) {
    auto c = dir / "putty.exe";
    if (std::filesystem::is_regular_file(c)) return c;
  }
#endif
  return std::nullopt;
}

std::optional<std::filesystem::path> find_winscp_executable(const std::string& custom_path) {
  if (!custom_path.empty()) {
    auto c = expand_user(custom_path);
    if (std::filesystem::is_regular_file(c)) return c;
  }
  if (auto found = which_exe("WinSCP")) return found;
  if (auto found = which_exe("winscp")) return found;
#ifdef _WIN32
  const char* pf = std::getenv("ProgramFiles");
  const char* pf86 = std::getenv("ProgramFiles(x86)");
  const char* local = std::getenv("LocalAppData");
  std::vector<std::filesystem::path> dirs;
  if (pf) dirs.push_back(std::filesystem::path(pf) / "WinSCP");
  if (pf86) dirs.push_back(std::filesystem::path(pf86) / "WinSCP");
  if (local) dirs.push_back(std::filesystem::path(local) / "Programs" / "WinSCP");
  for (const auto& dir : dirs) {
    auto c = dir / "WinSCP.exe";
    if (std::filesystem::is_regular_file(c)) return c;
  }
#endif
  return std::nullopt;
}

void open_putty_console(const Server& server, const std::string& putty_path) {
#ifndef _WIN32
  throw SSHError("PuTTY доступен только на Windows.");
#else
  auto putty = find_putty_executable(putty_path);
  if (!putty) throw PuttyNotFoundError("PuTTY не найден.");
  auto key_path = trim(server.key_path);
  auto password = trim(server.password);
  std::filesystem::path pwfile;
  std::string args = "-ssh " + server.host + " -P " + std::to_string(server.port ? server.port : 22) + " -l " +
                     server.username + " -noagent";
  if (!key_path.empty()) {
    std::filesystem::path ppk;
    try {
      ppk = resolve_putty_key(key_path);
    } catch (const SSHError&) {
      if (password.empty()) throw;
    }
    if (!ppk.empty()) {
      args += " -i \"" + ppk.string() + "\"";
    } else {
      pwfile = putty_password_file(password);
      args += " -pwfile \"" + pwfile.string() + "\"";
    }
  } else if (!password.empty()) {
    pwfile = putty_password_file(password);
    args += " -pwfile \"" + pwfile.string() + "\"";
  } else {
    throw SSHError("Для PuTTY укажите пароль или SSH-ключ в карточке VPS.");
  }
  try {
    launch_detached(*putty, args, "Не удалось запустить PuTTY");
  } catch (...) {
    if (!pwfile.empty()) {
      std::error_code ec;
      std::filesystem::remove(pwfile, ec);
    }
    throw;
  }
  if (!pwfile.empty()) schedule_unlink(pwfile);
#endif
}

void open_winscp(const Server& server, const std::string& winscp_path) {
#ifndef _WIN32
  throw SSHError("WinSCP доступен только на Windows.");
#else
  auto winscp = find_winscp_executable(winscp_path);
  if (!winscp) throw WinSCPNotFoundError("WinSCP не найден.");
  auto password = trim(server.password);
  auto ppk = try_resolve_key(server);
  if (!ppk && password.empty() && trim(server.key_path).empty()) {
    throw SSHError("Для WinSCP укажите пароль или SSH-ключ в карточке VPS.");
  }
  std::string url = make_sftp_url(server.username, ppk ? std::string{} : password, server.host,
                                  server.port ? server.port : 22);
  std::string session = server.name.empty() ? server.host : server.name;
  for (char& ch : session) {
    if (ch == '"') ch = '\'';
  }
  std::string args = url + " /sessionname=" + win_cmd_quote(session) + " /newinstance";
  if (ppk) {
    args += " /privatekey=" + win_cmd_quote(ppk->string());
  }
  launch_detached(*winscp, args, "Не удалось запустить WinSCP");
#endif
}

void open_extra_program(const ExtraProgram& program, const Server& server) {
#ifndef _WIN32
  throw SSHError("Внешние программы доступны только на Windows.");
#else
  auto exe = expand_user(program.path);
  if (!std::filesystem::is_regular_file(exe)) {
    throw ExternalProgramNotFoundError("Программа не найдена: " + program.path);
  }
  std::string ppk;
  if (program.args.find("{ppk}") != std::string::npos) {
    if (auto resolved = try_resolve_key(server)) ppk = resolved->string();
  }
  auto vars = program_placeholders(server, ppk);
  std::string args = expand_placeholders(program.args, vars);
  std::string fail = "Не удалось запустить " + program.name;
  launch_detached(exe, args, fail.c_str());
#endif
}

void open_system_console(const Server& server, const std::string& ssh_path) {
#ifndef _WIN32
  throw SSHError("Интерактивная консоль доступна только на Windows.");
#else
  auto ssh = find_ssh_executable(ssh_path);
  if (!ssh) {
    throw SSHError(
        "Не найден ssh.exe.\n"
        "Установите «OpenSSH Client»: Параметры → Приложения → Дополнительные компоненты.");
  }
  std::string args = "\"" + ssh->string() + "\" -t -o ServerAliveInterval=30 -o ServerAliveCountMax=4";
  if (!server.key_path.empty()) {
    auto key = expand_user(server.key_path);
    if (!std::filesystem::exists(key)) throw SSHError("SSH-ключ не найден: " + key.string());
    args += " -i \"" + key.string() + "\" -o IdentitiesOnly=yes";
  }
  args += " -p " + std::to_string(server.port ? server.port : 22) + " " + server.username + "@" + server.host;
  std::string title = std::string(kAppName) + " — " + (server.name.empty() ? server.host : server.name);
  for (char& ch : title) {
    if (ch == '&' || ch == '|' || ch == '<' || ch == '>' || ch == '^') ch = ' ';
  }
  if (title.size() > 80) title.resize(80);
  std::string cmdline = "title " + win_cmd_quote(title) + " & " + args;
  std::string full = "cmd.exe /k " + cmdline;
  STARTUPINFOA si{};
  PROCESS_INFORMATION pi{};
  si.cb = sizeof(si);
  std::filesystem::path cwd = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : ".";
  if (!CreateProcessA(nullptr, full.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, cwd.string().c_str(),
                      &si, &pi)) {
    throw SSHError("Не удалось открыть консоль");
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
#endif
}

}  // namespace fatty
