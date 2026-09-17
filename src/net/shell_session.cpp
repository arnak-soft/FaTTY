#include "net/shell_session.hpp"

#include "core/util.hpp"

#include <chrono>
#include <cstring>

#include <libssh2.h>

namespace fatty {
namespace {

LIBSSH2_SESSION* session_ptr(void* raw) {
  return static_cast<LIBSSH2_SESSION*>(ssh_libssh2_session(raw));
}

}  // namespace

ShellSession::ShellSession() = default;

ShellSession::~ShellSession() {
  stop();
}

void ShellSession::start(const Server& server, int cols, int rows, OutputCb on_output, ClosedCb on_closed) {
  stop();
  stop_ = false;
  running_ = true;
  {
    std::lock_guard lock(write_mu_);
    write_buf_.clear();
  }
  {
    std::lock_guard lock(resize_mu_);
    pending_cols_ = cols;
    pending_rows_ = rows;
    resize_pending_ = false;
  }
  thread_ = std::thread([this, server, cols, rows, on_output = std::move(on_output),
                         on_closed = std::move(on_closed)]() mutable {
    worker(server, cols, rows, std::move(on_output), std::move(on_closed));
  });
}

void ShellSession::write(std::string data) {
  if (data.empty() || !running_) return;
  {
    std::lock_guard lock(write_mu_);
    write_buf_.append(std::move(data));
  }
  write_cv_.notify_one();
}

void ShellSession::resize(int cols, int rows) {
  if (cols < 2 || rows < 2) return;
  {
    std::lock_guard lock(resize_mu_);
    pending_cols_ = cols;
    pending_rows_ = rows;
    resize_pending_ = true;
  }
  write_cv_.notify_one();
}

void ShellSession::stop() {
  stop_ = true;
  write_cv_.notify_one();
  if (thread_.joinable()) thread_.join();
  running_ = false;
}

void ShellSession::worker(Server server, int cols, int rows, OutputCb on_output, ClosedCb on_closed) {
  void* raw = nullptr;
  LIBSSH2_CHANNEL* channel = nullptr;
  std::string close_reason;
  try {
    raw = ssh_connect_raw(server);
    LIBSSH2_SESSION* session = session_ptr(raw);
    if (!session) throw SSHError("Нет SSH-сессии");

    channel = libssh2_channel_open_session(session);
    if (!channel) throw SSHError("Не удалось открыть канал");

    const char* term = "xterm-256color";
    if (libssh2_channel_request_pty_ex(channel, term, static_cast<unsigned int>(std::strlen(term)), nullptr, 0,
                                       cols > 0 ? cols : 80, rows > 0 ? rows : 24, 0, 0) != 0) {
      throw SSHError("Не удалось запросить PTY");
    }
    if (libssh2_channel_shell(channel) != 0) {
      throw SSHError("Не удалось запустить shell");
    }
    libssh2_channel_set_blocking(channel, 0);

    while (!stop_) {
      {
        int pc = 0;
        int pr = 0;
        bool do_resize = false;
        {
          std::lock_guard lock(resize_mu_);
          if (resize_pending_) {
            pc = pending_cols_;
            pr = pending_rows_;
            resize_pending_ = false;
            do_resize = true;
          }
        }
        if (do_resize && pc >= 2 && pr >= 2) {
          libssh2_channel_request_pty_size(channel, pc, pr);
        }
      }

      {
        std::string pending;
        {
          std::unique_lock lock(write_mu_);
          if (write_buf_.empty() && !stop_) {
            write_cv_.wait_for(lock, std::chrono::milliseconds(20));
          }
          if (!write_buf_.empty()) {
            pending.swap(write_buf_);
          }
        }
        std::size_t off = 0;
        while (off < pending.size() && !stop_) {
          ssize_t n = libssh2_channel_write(channel, pending.data() + off, pending.size() - off);
          if (n == LIBSSH2_ERROR_EAGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
          }
          if (n < 0) {
            close_reason = "Ошибка записи в shell";
            stop_ = true;
            break;
          }
          off += static_cast<std::size_t>(n);
        }
      }

      char buf[4096];
      bool got = false;
      while (!stop_) {
        ssize_t n = libssh2_channel_read(channel, buf, sizeof(buf));
        if (n == LIBSSH2_ERROR_EAGAIN) break;
        if (n < 0) {
          close_reason = "Ошибка чтения shell";
          stop_ = true;
          break;
        }
        if (n == 0) break;
        got = true;
        if (on_output) on_output(std::string(buf, static_cast<std::size_t>(n)));
      }
      while (!stop_) {
        ssize_t n = libssh2_channel_read_stderr(channel, buf, sizeof(buf));
        if (n == LIBSSH2_ERROR_EAGAIN) break;
        if (n < 0) break;
        if (n == 0) break;
        got = true;
        if (on_output) on_output(std::string(buf, static_cast<std::size_t>(n)));
      }

      if (libssh2_channel_eof(channel)) {
        close_reason = "Shell закрыт";
        break;
      }
      if (!got && !stop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
      }
    }
  } catch (const SSHError& e) {
    close_reason = e.what();
  } catch (const std::exception& e) {
    close_reason = e.what();
  } catch (...) {
    close_reason = "Неизвестная ошибка shell";
  }

  if (channel) {
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
  }
  if (raw) ssh_close_raw(raw);
  running_ = false;
  if (on_closed) {
    if (close_reason.empty() && stop_) close_reason = "Отключено";
    on_closed(close_reason.empty() ? "Отключено" : close_reason);
  }
}

}  // namespace fatty
