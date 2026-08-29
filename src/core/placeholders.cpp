#include "core/placeholders.hpp"

#include "core/util.hpp"

#include <cctype>

namespace fatty {

std::string percent_encode(std::string_view text) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size() * 3);
  for (unsigned char ch : text) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      out.push_back(static_cast<char>(ch));
    } else {
      out.push_back('%');
      out.push_back(hex[ch >> 4]);
      out.push_back(hex[ch & 0xf]);
    }
  }
  return out;
}

std::string expand_placeholders(const std::string& tmpl, const std::map<std::string, std::string>& vars) {
  std::string out;
  out.reserve(tmpl.size());
  for (std::size_t i = 0; i < tmpl.size();) {
    if (tmpl[i] == '{') {
      auto end = tmpl.find('}', i + 1);
      if (end != std::string::npos) {
        std::string key = tmpl.substr(i + 1, end - i - 1);
        auto it = vars.find(key);
        if (it != vars.end()) {
          out += it->second;
          i = end + 1;
          continue;
        }
      }
    }
    out.push_back(tmpl[i]);
    ++i;
  }
  return out;
}

std::string make_sftp_url(const std::string& user, const std::string& password, const std::string& host,
                          int port) {
  int p = port > 0 ? port : 22;
  std::string hostpart = host;
  if (host.find(':') != std::string::npos) {
    hostpart = "[" + host + "]";
  }
  std::string url = "sftp://" + percent_encode(user);
  if (!password.empty()) {
    url += ":" + percent_encode(password);
  }
  url += "@" + hostpart + ":" + std::to_string(p) + "/";
  return url;
}

std::map<std::string, std::string> program_placeholders(const Server& server, const std::string& ppk) {
  int port = server.port > 0 ? server.port : 22;
  std::string display = server.name.empty() ? server.host : server.name;
  std::map<std::string, std::string> vars;
  vars["host"] = server.host;
  vars["port"] = std::to_string(port);
  vars["user"] = server.username;
  vars["username"] = server.username;
  vars["password"] = server.password;
  vars["name"] = display;
  vars["key"] = server.key_path;
  vars["key_path"] = server.key_path;
  vars["ppk"] = ppk;
  vars["sftp_url"] = make_sftp_url(server.username, server.password, server.host, port);
  vars["ssh_target"] = server.username + "@" + server.host;
  return vars;
}

}  // namespace fatty
