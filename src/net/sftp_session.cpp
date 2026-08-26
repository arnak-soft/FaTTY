#include "net/sftp_session.hpp"

#include "core/util.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <regex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <libssh2.h>
#include <libssh2_sftp.h>

namespace fatty {

std::string RemoteEntry::kind_label() const {
  if (is_link && is_dir) return "ссылка → папка";
  if (is_link) return "ссылка";
  return is_dir ? "папка" : "файл";
}

std::string format_size(long long n) {
  if (n < 0) return "—";
  if (n < 1024) return std::to_string(n) + " Б";
  const char* units[] = {"КБ", "МБ", "ГБ"};
  long long div = 1024;
  for (int i = 0; i < 3; ++i) {
    if (n < div * 1024 || i == 2) {
      double value = static_cast<double>(n) / static_cast<double>(div);
      char buf[32];
      if (value < 10) {
        std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[i]);
      } else {
        std::snprintf(buf, sizeof(buf), "%.0f %s", value, units[i]);
      }
      return buf;
    }
    div *= 1024;
  }
  return std::to_string(n) + " Б";
}

std::string format_mtime(long long ts) {
  if (ts <= 0) return "—";
  std::time_t t = static_cast<std::time_t>(ts);
  std::tm tm{};
#ifdef _WIN32
  if (localtime_s(&tm, &t) != 0) return "—";
#else
  if (!localtime_r(&t, &tm)) return "—";
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", &tm);
  return buf;
}

std::string guess_start_path(const std::vector<Command>& commands) {
  static const std::regex cd_re(R"((?:^|[;&|\n])\s*cd\s+(/[^\s;&|]+))");
  for (const auto& command : commands) {
    std::smatch m;
    if (std::regex_search(command.command, m, cd_re)) {
      return m[1].str();
    }
  }
  return ".";
}

SFTPSession::SFTPSession() = default;

SFTPSession::~SFTPSession() {
  close();
}

void SFTPSession::connect(const Server& server, const std::string& start_path) {
  cancel_ = false;
  raw_ = ssh_connect_raw(server);
  auto* session = static_cast<LIBSSH2_SESSION*>(ssh_libssh2_session(raw_));
  sftp_ = libssh2_sftp_init(session);
  if (!sftp_) {
    close();
    throw SFTPError("Не удалось открыть SFTP. На сервере должна быть включена подсистема sftp.");
  }
  chdir(".");
  auto wanted = trim(start_path);
  if (!wanted.empty() && wanted != "." && wanted != remote_cwd) {
    try {
      chdir(wanted);
    } catch (const SFTPError&) {
    }
  }
}

void SFTPSession::close() {
  cancel_ = true;
  if (sftp_) {
    libssh2_sftp_shutdown(static_cast<LIBSSH2_SFTP*>(sftp_));
    sftp_ = nullptr;
  }
  if (raw_) {
    ssh_close_raw(raw_);
    raw_ = nullptr;
  }
  remote_cwd.clear();
}

void SFTPSession::cancel_transfer() {
  cancel_ = true;
}

void SFTPSession::chdir(const std::string& path) {
  auto* sftp = static_cast<LIBSSH2_SFTP*>(sftp_);
  if (!sftp) throw SFTPError("SFTP-сессия закрыта");
  auto* handle = libssh2_sftp_opendir(sftp, path.c_str());
  if (!handle) {
    throw SFTPError("Не удалось открыть каталог «" + path + "»");
  }
  libssh2_sftp_closedir(handle);
  char buf[1024];
  int n = libssh2_sftp_realpath(sftp, path.c_str(), buf, sizeof(buf));
  if (n > 0) {
    remote_cwd.assign(buf, static_cast<std::size_t>(n));
  } else {
    remote_cwd = path;
  }
}

std::vector<RemoteEntry> SFTPSession::listdir() {
  std::lock_guard lock(mutex_);
  auto* sftp = static_cast<LIBSSH2_SFTP*>(sftp_);
  if (!sftp) throw SFTPError("SFTP-сессия закрыта");
  std::string cwd = remote_cwd.empty() ? "." : remote_cwd;
  auto* handle = libssh2_sftp_opendir(sftp, cwd.c_str());
  if (!handle) throw SFTPError("Не удалось прочитать каталог");
  std::vector<RemoteEntry> entries;
  char name[512];
  char longentry[512];
  LIBSSH2_SFTP_ATTRIBUTES attrs{};
  while (libssh2_sftp_readdir_ex(handle, name, sizeof(name), longentry, sizeof(longentry), &attrs) > 0) {
    std::string nm = name;
    if (nm.empty() || nm == "." || nm == "..") continue;
    RemoteEntry e;
    e.name = nm;
    e.path = posix_join(cwd, nm);
    e.is_link = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISLNK(attrs.permissions);
    e.is_dir = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
    if (e.is_link) {
      LIBSSH2_SFTP_ATTRIBUTES followed{};
      auto full = posix_join(cwd, nm);
      if (libssh2_sftp_stat(sftp, full.c_str(), &followed) == 0) {
        e.is_dir = (followed.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(followed.permissions);
      }
    }
    e.size = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? static_cast<long long>(attrs.filesize) : 0;
    e.mtime = (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? static_cast<long long>(attrs.mtime) : 0;
    entries.push_back(std::move(e));
  }
  libssh2_sftp_closedir(handle);
  std::sort(entries.begin(), entries.end(), [](const RemoteEntry& a, const RemoteEntry& b) {
    if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
    return to_lower(a.name) < to_lower(b.name);
  });
  return entries;
}

void SFTPSession::enter(const std::string& path) {
  std::lock_guard lock(mutex_);
  chdir(path);
}

void SFTPSession::go_up() {
  std::lock_guard lock(mutex_);
  auto cwd = remote_cwd.empty() ? "/" : remote_cwd;
  if (cwd == "/") return;
  chdir(posix_parent(cwd));
}

void SFTPSession::mkdir(const std::string& name_in) {
  auto name = trim(name_in);
  while (!name.empty() && (name.back() == '/' || name.back() == '\\')) name.pop_back();
  if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
    throw SFTPError("Укажите имя папки без слэшей.");
  }
  std::lock_guard lock(mutex_);
  auto* sftp = static_cast<LIBSSH2_SFTP*>(sftp_);
  if (!sftp) throw SFTPError("SFTP-сессия закрыта");
  auto path = posix_join(remote_cwd, name);
  if (libssh2_sftp_mkdir(sftp, path.c_str(), 0755) != 0) {
    throw SFTPError("Не удалось создать папку");
  }
}

void SFTPSession::remove(const RemoteEntry& entry) {
  std::lock_guard lock(mutex_);
  auto* sftp = static_cast<LIBSSH2_SFTP*>(sftp_);
  if (!sftp) throw SFTPError("SFTP-сессия закрыта");
  auto path = posix_join(remote_cwd, entry.name);
  int rc = (entry.is_dir && !entry.is_link) ? libssh2_sftp_rmdir(sftp, path.c_str())
                                           : libssh2_sftp_unlink(sftp, path.c_str());
  if (rc != 0) {
    throw SFTPError("Не удалось удалить «" + entry.name + "»");
  }
}

bool SFTPSession::exists(const std::string& name) {
  std::lock_guard lock(mutex_);
  auto* sftp = static_cast<LIBSSH2_SFTP*>(sftp_);
  if (!sftp) throw SFTPError("SFTP-сессия закрыта");
  LIBSSH2_SFTP_ATTRIBUTES attrs{};
  auto path = posix_join(remote_cwd, name);
  return libssh2_sftp_stat(sftp, path.c_str(), &attrs) == 0;
}

void SFTPSession::upload(const std::filesystem::path& local, const std::string& remote_name, ProgressCb on_progress) {
  if (!std::filesystem::is_regular_file(local)) {
    throw SFTPError("Локальный файл не найден: " + local.string());
  }
  transfer(local.string(), posix_join(remote_cwd, remote_name), false, static_cast<long long>(std::filesystem::file_size(local)),
           std::move(on_progress));
}

void SFTPSession::download(const std::string& remote_name, const std::filesystem::path& local, long long size,
                           ProgressCb on_progress) {
  std::filesystem::create_directories(local.parent_path());
  transfer(local.string(), posix_join(remote_cwd, remote_name), true, std::max(0LL, size), std::move(on_progress));
}

void SFTPSession::transfer(const std::string& local, const std::string& remote, bool download, long long size,
                           ProgressCb on_progress) {
  cancel_ = false;
  std::lock_guard lock(mutex_);
  auto* sftp = static_cast<LIBSSH2_SFTP*>(sftp_);
  if (!sftp) throw SFTPError("SFTP-сессия закрыта");
  LIBSSH2_SFTP_HANDLE* handle = nullptr;
  std::fstream file;
  try {
    if (download) {
      handle = libssh2_sftp_open(sftp, remote.c_str(), LIBSSH2_FXF_READ, 0);
      if (!handle) throw SFTPError("Не удалось передать файл");
      file.open(local, std::ios::binary | std::ios::out | std::ios::trunc);
    } else {
      handle = libssh2_sftp_open(sftp, remote.c_str(),
                                 LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0644);
      if (!handle) throw SFTPError("Не удалось передать файл");
      file.open(local, std::ios::binary | std::ios::in);
    }
    if (!file) throw SFTPError("Не удалось передать файл");
    char buf[32768];
    long long sent = 0;
    auto last = std::chrono::steady_clock::now();
    while (true) {
      if (cancel_) throw TransferCancelled();
      ssize_t n = 0;
      if (download) {
        n = libssh2_sftp_read(handle, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) throw SFTPError("Не удалось передать файл");
        file.write(buf, n);
        sent += n;
      } else {
        file.read(buf, sizeof(buf));
        n = static_cast<ssize_t>(file.gcount());
        if (n <= 0) break;
        ssize_t off = 0;
        while (off < n) {
          auto w = libssh2_sftp_write(handle, buf + off, static_cast<std::size_t>(n - off));
          if (w < 0) throw SFTPError("Не удалось передать файл");
          off += w;
          sent += w;
        }
      }
      auto now = std::chrono::steady_clock::now();
      if (on_progress && (sent >= size || now - last >= std::chrono::milliseconds(50))) {
        last = now;
        on_progress(sent, size ? size : sent);
      }
    }
  } catch (const TransferCancelled&) {
    if (handle) libssh2_sftp_close(handle);
    file.close();
    if (download) {
      std::error_code ec;
      std::filesystem::remove(local, ec);
    } else if (sftp) {
      libssh2_sftp_unlink(sftp, remote.c_str());
    }
    throw;
  } catch (...) {
    if (handle) libssh2_sftp_close(handle);
    throw;
  }
  if (handle) libssh2_sftp_close(handle);
}

}  // namespace fatty
