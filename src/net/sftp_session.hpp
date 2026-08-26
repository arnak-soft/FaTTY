#pragma once

#include "core/store.hpp"
#include "net/ssh_session.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace fatty {

class SFTPError : public SSHError {
 public:
  using SSHError::SSHError;
};

class TransferCancelled : public SFTPError {
 public:
  TransferCancelled() : SFTPError("Передача прервана") {}
};

struct RemoteEntry {
  std::string name;
  std::string path;
  bool is_dir = false;
  long long size = 0;
  long long mtime = 0;
  bool is_link = false;
  std::string kind_label() const;
};

std::string format_size(long long n);
std::string format_mtime(long long ts);
std::string guess_start_path(const std::vector<Command>& commands);

class SFTPSession {
 public:
  using ProgressCb = std::function<void(long long sent, long long total)>;

  SFTPSession();
  ~SFTPSession();

  void connect(const Server& server, const std::string& start_path = ".");
  void close();
  void cancel_transfer();
  std::vector<RemoteEntry> listdir();
  void enter(const std::string& path);
  void go_up();
  void mkdir(const std::string& name);
  void remove(const RemoteEntry& entry);
  bool exists(const std::string& name);
  void upload(const std::filesystem::path& local, const std::string& remote_name, ProgressCb on_progress);
  void download(const std::string& remote_name, const std::filesystem::path& local, long long size,
                ProgressCb on_progress);

  std::string remote_cwd;

 private:
  void chdir(const std::string& path);
  void transfer(const std::string& local, const std::string& remote, bool download, long long size,
                ProgressCb on_progress);

  std::mutex mutex_;
  std::atomic<bool> cancel_{false};
  void* raw_ = nullptr;
  void* sftp_ = nullptr;  // LIBSSH2_SFTP*
};

}  // namespace fatty
