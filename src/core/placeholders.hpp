#pragma once

#include "core/store.hpp"

#include <map>
#include <string>
#include <string_view>

namespace fatty {

std::string percent_encode(std::string_view text);
std::string expand_placeholders(const std::string& tmpl,
                                const std::map<std::string, std::string>& vars);
std::string make_sftp_url(const std::string& user, const std::string& password, const std::string& host,
                          int port);
std::map<std::string, std::string> program_placeholders(const Server& server,
                                                        const std::string& ppk = {});

}  // namespace fatty
