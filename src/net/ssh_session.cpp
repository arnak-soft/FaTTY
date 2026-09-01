#include "net/ssh_session.hpp"

#include "core/paths.hpp"
#include "core/quote.hpp"
#include "core/util.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using SOCKET = int;
constexpr int INVALID_SOCKET = -1;
#endif

#include <libssh2.h>
#include <libssh2_sftp.h>

namespace fatty {
namespace {

std::once_flag g_ssh_once;
std::once_flag g_wsa_once;

void ensure_net() {
#ifdef _WIN32
  std::call_once(g_wsa_once, [] {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
  });
#endif
  std::call_once(g_ssh_once, [] { libssh2_init(0); });
}

void close_socket(SOCKET s) {
  if (s == INVALID_SOCKET) return;
#ifdef _WIN32
  closesocket(s);
#else
  close(s);
#endif
}

SOCKET tcp_connect(const std::string& host, int port, int timeout_sec) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  addrinfo* res = nullptr;
  auto port_s = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res) != 0 || !res) {
    throw SSHError("Не удалось разрешить хост: " + host);
  }
  SOCKET sock = INVALID_SOCKET;
  for (auto* p = res; p; p = p->ai_next) {
    sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock == INVALID_SOCKET) continue;
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(sock, FIONBIO, &nb);
#else
    // leave blocking; timeout via SO_SNDTIMEO
#endif
    int rc = connect(sock, p->ai_addr, static_cast<int>(p->ai_addrlen));
#ifdef _WIN32
    if (rc == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(sock, &fds);
      timeval tv{};
      tv.tv_sec = timeout_sec;
      if (select(0, nullptr, &fds, nullptr, &tv) > 0) {
        nb = 0;
        ioctlsocket(sock, FIONBIO, &nb);
        break;
      }
    } else if (rc == 0) {
      nb = 0;
      ioctlsocket(sock, FIONBIO, &nb);
      break;
    }
    close_socket(sock);
    sock = INVALID_SOCKET;
#else
    if (rc == 0) break;
    close_socket(sock);
    sock = INVALID_SOCKET;
#endif
  }
  freeaddrinfo(res);
  if (sock == INVALID_SOCKET) {
    throw SSHError("Не удалось подключиться к " + host + ":" + port_s);
  }
  return sock;
}

struct SessionOwner {
  SOCKET sock = INVALID_SOCKET;
  LIBSSH2_SESSION* session = nullptr;
};

void apply_known_hosts(LIBSSH2_SESSION* session, const std::string& host, int port) {
  std::filesystem::create_directories(known_hosts_path().parent_path());
  LIBSSH2_KNOWNHOSTS* hosts = libssh2_knownhost_init(session);
  if (!hosts) return;
  if (std::filesystem::exists(known_hosts_path())) {
    libssh2_knownhost_readfile(hosts, known_hosts_path().string().c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
  }
  size_t len = 0;
  int type = 0;
  const char* key = libssh2_session_hostkey(session, &len, &type);
  if (key) {
    int check = libssh2_knownhost_checkp(
        hosts, host.c_str(), port, key, len, LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW, nullptr);
    if (check == LIBSSH2_KNOWNHOST_CHECK_NOTFOUND || check == LIBSSH2_KNOWNHOST_CHECK_MISMATCH) {
      int typemask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW;
      if (type == LIBSSH2_HOSTKEY_TYPE_RSA) typemask |= LIBSSH2_KNOWNHOST_KEY_SSHRSA;
      else if (type == LIBSSH2_HOSTKEY_TYPE_ECDSA_256) typemask |= LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
      else if (type == LIBSSH2_HOSTKEY_TYPE_ECDSA_384) typemask |= LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
      else if (type == LIBSSH2_HOSTKEY_TYPE_ECDSA_521) typemask |= LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
      else if (type == LIBSSH2_HOSTKEY_TYPE_ED25519) typemask |= LIBSSH2_KNOWNHOST_KEY_ED25519;
      libssh2_knownhost_addc(hosts, host.c_str(), nullptr, key, len, nullptr, 0, typemask, nullptr);
      libssh2_knownhost_writefile(hosts, known_hosts_path().string().c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    }
  }
  libssh2_knownhost_free(hosts);
}

bool auth_agent(LIBSSH2_SESSION* session, const std::string& username) {
  LIBSSH2_AGENT* agent = libssh2_agent_init(session);
  if (!agent) return false;
  bool ok = false;
  if (libssh2_agent_connect(agent) == 0 && libssh2_agent_list_identities(agent) == 0) {
    libssh2_agent_publickey* identity = nullptr;
    libssh2_agent_publickey* prev = nullptr;
    while (libssh2_agent_get_identity(agent, &identity, prev) == 0 && identity) {
      if (libssh2_agent_userauth(agent, username.c_str(), identity) == 0) {
        ok = true;
        break;
      }
      prev = identity;
    }
  }
  libssh2_agent_disconnect(agent);
  libssh2_agent_free(agent);
  return ok;
}

LIBSSH2_SESSION* handshake_and_auth(SOCKET sock, const Server& server) {
  LIBSSH2_SESSION* session = libssh2_session_init();
  if (!session) {
    throw SSHError("Не удалось создать SSH-сессию");
  }
  libssh2_session_set_blocking(session, 1);
  libssh2_session_set_timeout(session, 20000);
  if (libssh2_session_handshake(session, static_cast<libssh2_socket_t>(sock)) != 0) {
    char* msg = nullptr;
    libssh2_session_last_error(session, &msg, nullptr, 0);
    std::string err = msg ? msg : "handshake";
    libssh2_session_free(session);
    throw SSHError("Не удалось подключиться: " + err);
  }
  apply_known_hosts(session, server.host, server.port);
  bool authed = false;
  std::string last;
  if (!server.key_path.empty()) {
    auto key = expand_user(server.key_path);
    if (!std::filesystem::exists(key)) {
      libssh2_session_disconnect(session, "bye");
      libssh2_session_free(session);
      throw SSHError("SSH-ключ не найден: " + key.string());
    }
    int rc = libssh2_userauth_publickey_fromfile(
        session, server.username.c_str(), nullptr, key.string().c_str(), nullptr);
    if (rc == 0) {
      authed = true;
    } else {
      char* msg = nullptr;
      libssh2_session_last_error(session, &msg, nullptr, 0);
      last = msg ? msg : "publickey";
    }
  }
  if (!authed && !server.password.empty()) {
    if (libssh2_userauth_password(session, server.username.c_str(), server.password.c_str()) == 0) {
      authed = true;
    } else {
      char* msg = nullptr;
      libssh2_session_last_error(session, &msg, nullptr, 0);
      last = msg ? msg : "password";
    }
  }
  if (!authed && server.key_path.empty()) {
    if (auth_agent(session, server.username)) authed = true;
  }
  if (!authed) {
    libssh2_session_disconnect(session, "bye");
    libssh2_session_free(session);
    throw SSHError("Не удалось подключиться: " + (last.empty() ? std::string("аутентификация") : last));
  }
  libssh2_keepalive_config(session, 1, 15);
  return session;
}

thread_local SessionOwner tls_owner;

}  // namespace

void* ssh_connect_raw(const Server& server) {
  ensure_net();
  auto* owner = new SessionOwner();
  try {
    owner->sock = tcp_connect(server.host, server.port ? server.port : 22, 20);
    owner->session = handshake_and_auth(owner->sock, server);
  } catch (...) {
    close_socket(owner->sock);
    delete owner;
    throw;
  }
  return owner;
}

void ssh_close_raw(void* raw) {
  if (!raw) return;
  auto* owner = static_cast<SessionOwner*>(raw);
  if (owner->session) {
    libssh2_session_disconnect(owner->session, "bye");
    libssh2_session_free(owner->session);
  }
  close_socket(owner->sock);
  delete owner;
}

LIBSSH2_SESSION* session_of(void* raw) {
  return raw ? static_cast<SessionOwner*>(raw)->session : nullptr;
}

void* ssh_libssh2_session(void* raw) {
  return session_of(raw);
}

SSHSession::SSHSession() = default;

SSHSession::~SSHSession() {
  cancel();
}

void SSHSession::cancel() {
  // Только выставляем флаг. libssh2 не потокобезопасен в рамках одной сессии,
  // поэтому закрывать канал отсюда (из GUI-потока) нельзя: рабочий поток в этот
  // момент может сидеть в libssh2_channel_read. Цикл run() опрашивает флаг
  // каждые 40 мс и сам корректно закрывает канал.
  cancel_ = true;
}

RunResult SSHSession::run(const Server& server, const std::string& command, int timeout_sec, bool login_shell,
                          const OutputCb& on_output, const std::string& cwd_in, std::string_view shell) {
  cancel_ = false;
  std::string cwd = trim(cwd_in);
  std::string where = cwd.empty() ? "" : ("  " + cwd);
  on_output("→ " + server.username + "@" + server.host + ":" + std::to_string(server.port) + where + "\n");
  void* raw = nullptr;
  try {
    raw = ssh_connect_raw(server);
    session_.store(raw);
  } catch (const SSHError&) {
    session_.store(nullptr);
    throw;
  }
  auto [remote, mark] = wrap_remote_command(command, cwd, login_shell, shell);
  CwdOutputFilter filt(mark, on_output);
  on_output("$ " + trim(command) + "\n\n");
  LIBSSH2_SESSION* session = session_of(raw);
  LIBSSH2_CHANNEL* channel = libssh2_channel_open_session(session);
  if (!channel) {
    ssh_close_raw(raw);
    session_.store(nullptr);
    throw SSHError("Не удалось запустить команду: нет канала");
  }
  channel_.store(channel);
  if (libssh2_channel_exec(channel, remote.c_str()) != 0) {
    channel_.store(nullptr);
    libssh2_channel_free(channel);
    ssh_close_raw(raw);
    session_.store(nullptr);
    throw SSHError("Не удалось запустить команду");
  }
  libssh2_channel_set_blocking(channel, 0);
  auto started = std::chrono::steady_clock::now();
  std::string stdout_buf;
  std::string stderr_buf;
  auto flush = [&](std::string& buf, bool is_stdout) {
    if (buf.empty()) return;
    if (is_stdout) filt.feed(buf);
    else on_output(buf);
    buf.clear();
  };
  auto finish_output = [&] {
    flush(stdout_buf, true);
    flush(stderr_buf, false);
    filt.finish();
  };
  RunResult result;
  try {
    while (true) {
      if (cancel_) {
        finish_output();
        on_output("\n■ Выполнение прервано\n");
        result.exit_code = 130;
        result.cwd = filt.cwd().empty() ? cwd : filt.cwd();
        break;
      }
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
      if (timeout_sec > 0 && elapsed > timeout_sec) {
        finish_output();
        on_output("\n■ Таймаут " + std::to_string(timeout_sec) + " с\n");
        result.exit_code = 124;
        result.cwd = filt.cwd().empty() ? cwd : filt.cwd();
        break;
      }
      char buf[4096];
      ssize_t n = libssh2_channel_read(channel, buf, sizeof(buf));
      bool got = false;
      if (n > 0) {
        stdout_buf.append(buf, static_cast<std::size_t>(n));
        if (stdout_buf.find('\n') != std::string::npos || stdout_buf.size() > 2048) {
          flush(stdout_buf, true);
        }
        got = true;
      }
      n = libssh2_channel_read_stderr(channel, buf, sizeof(buf));
      if (n > 0) {
        stderr_buf.append(buf, static_cast<std::size_t>(n));
        if (stderr_buf.find('\n') != std::string::npos || stderr_buf.size() > 2048) {
          flush(stderr_buf, false);
        }
        got = true;
      }
      if (libssh2_channel_eof(channel)) {
        finish_output();
        result.exit_code = static_cast<int>(libssh2_channel_get_exit_status(channel));
        result.cwd = filt.cwd().empty() ? cwd : filt.cwd();
        break;
      }
      if (!got) {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
      }
    }
  } catch (...) {
    channel_.store(nullptr);
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    ssh_close_raw(raw);
    session_.store(nullptr);
    throw;
  }
  channel_.store(nullptr);
  libssh2_channel_close(channel);
  libssh2_channel_free(channel);
  ssh_close_raw(raw);
  session_.store(nullptr);
  return result;
}

}  // namespace fatty
