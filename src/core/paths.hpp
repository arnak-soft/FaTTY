#pragma once

#include <filesystem>
#include <string>

namespace fatty {

const std::filesystem::path& app_dir();
const std::filesystem::path& config_path();
const std::filesystem::path& known_hosts_path();
const std::filesystem::path& journal_path();
const std::filesystem::path& lockout_path();
const std::filesystem::path& error_log_path();
const std::filesystem::path& putty_keys_dir();

std::filesystem::path resource_root();
std::filesystem::path exe_dir();

void open_directory(const std::filesystem::path& path);
void open_path(const std::filesystem::path& path);
void open_url(const std::string& url);

}  // namespace fatty
