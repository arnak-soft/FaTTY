#include "core/quote.hpp"

#include "core/store.hpp"
#include "core/util.hpp"

#include <algorithm>
#include <cctype>

namespace fatty {

std::string shlex_quote(const std::string& value) {
  if (value.empty()) {
    return "''";
  }
  bool safe = true;
  for (unsigned char ch : value) {
    if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '/' || ch == '=' || ch == '+' ||
          ch == ':' || ch == '@' || ch == '%')) {
      safe = false;
      break;
    }
  }
  if (safe) {
    return value;
  }
  std::string out = "'";
  for (char ch : value) {
    if (ch == '\'') {
      out += "'\"'\"'";
    } else {
      out += ch;
    }
  }
  out += "'";
  return out;
}

std::pair<std::string, std::string> wrap_remote_command(const std::string& command, const std::string& cwd_in,
                                                        bool login_shell, std::string_view shell) {
  std::string mark = "FATTYCWD_" + new_uuid();
  // uuid has dashes; Python uses uuid4().hex (no dashes)
  mark.erase(std::remove(mark.begin(), mark.end(), '-'), mark.end());
  mark += ":";
  std::string lines = "set +e";
  std::string cwd = trim(cwd_in);
  if (!cwd.empty()) {
    std::string warn = "■ Нет каталога " + cwd + " — стартую из домашней";
    std::string cd_arg;
    if (cwd == "~") {
      cd_arg = "~";
    } else if (cwd.size() >= 2 && cwd[0] == '~' && cwd[1] == '/') {
      cd_arg = "~/" + shlex_quote(cwd.substr(2));
    } else {
      cd_arg = shlex_quote(cwd);
    }
    lines += "\ncd " + cd_arg + " || printf '%s\\n' " + shlex_quote(warn);
  }
  lines += "\n" + trim(command);
  lines += "\n_fatty_st=$?";
  lines += "\nprintf '\\n" + mark + "%s\\n' \"$(pwd 2>/dev/null || true)\"";
  lines += "\nexit $_fatty_st";
  std::string flag = login_shell ? "-lc" : "-c";
  const std::string sh = normalize_remote_shell(shell);
  return {sh + " " + flag + " " + shlex_quote(lines), mark};
}

CwdOutputFilter::CwdOutputFilter(std::string mark, std::function<void(const std::string&)> on_output)
    : mark_(std::move(mark)), on_output_(std::move(on_output)) {}

void CwdOutputFilter::feed(const std::string& text) {
  std::string data = hold_ + text;
  hold_.clear();
  std::size_t start = 0;
  while (true) {
    auto pos = data.find('\n', start);
    if (pos == std::string::npos) {
      hold_ = data.substr(start);
      break;
    }
    std::string line = data.substr(start, pos - start);
    start = pos + 1;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind(mark_, 0) == 0) {
      cwd_ = trim(line.substr(mark_.size()));
    } else {
      on_output_(line + "\n");
    }
  }
}

void CwdOutputFilter::finish() {
  if (hold_.empty()) return;
  std::string body = hold_;
  if (!body.empty() && body.back() == '\r') body.pop_back();
  if (body.rfind(mark_, 0) == 0) {
    cwd_ = trim(body.substr(mark_.size()));
  } else {
    on_output_(hold_);
  }
  hold_.clear();
}

}  // namespace fatty
