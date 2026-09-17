#pragma once

#include "core/store.hpp"
#include "net/ssh_session.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace fatty {

// Интерактивный login-shell через libssh2 PTY. Отдельное соединение —
// не делит AppFrame::session_ с F5/очередью.
class ShellSession {
 public:
  using OutputCb = std::function<void(const std::string&)>;
  using ClosedCb = std::function<void(const std::string& reason)>;

  ShellSession();
  ~ShellSession();
  ShellSession(const ShellSession&) = delete;
  ShellSession& operator=(const ShellSession&) = delete;

  bool running() const { return running_.load(); }

  // cols/rows — начальный размер PTY. Колбэки могут вызываться с рабочего потока.
  void start(const Server& server, int cols, int rows, OutputCb on_output, ClosedCb on_closed);
  void write(std::string data);
  void resize(int cols, int rows);
  void stop();

 private:
  void worker(Server server, int cols, int rows, OutputCb on_output, ClosedCb on_closed);

  std::atomic<bool> stop_{false};
  std::atomic<bool> running_{false};
  std::thread thread_;

  std::mutex write_mu_;
  std::string write_buf_;
  std::condition_variable write_cv_;

  std::mutex resize_mu_;
  int pending_cols_ = 0;
  int pending_rows_ = 0;
  bool resize_pending_ = false;
};

}  // namespace fatty
