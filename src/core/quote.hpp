#pragma once

#include <functional>
#include <string>
#include <utility>

namespace fatty {

std::string shlex_quote(const std::string& value);
std::pair<std::string, std::string> wrap_remote_command(const std::string& command, const std::string& cwd,
                                                        bool login_shell);

class CwdOutputFilter {
 public:
  CwdOutputFilter(std::string mark, std::function<void(const std::string&)> on_output);
  void feed(const std::string& text);
  void finish();
  const std::string& cwd() const { return cwd_; }

 private:
  std::string mark_;
  std::function<void(const std::string&)> on_output_;
  std::string cwd_;
  std::string hold_;
};

}  // namespace fatty
