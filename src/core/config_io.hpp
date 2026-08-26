#pragma once

#include "core/store.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

namespace fatty {

struct ImportResult {
  int servers_added = 0;
  int servers_skipped = 0;
  int servers_replaced = 0;
  int commands_added = 0;
  int commands_skipped = 0;
  bool settings_applied = false;
};

class ConfigIOError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

void write_export(const std::filesystem::path& path, const Config& config, bool include_secrets,
                  bool include_settings);
nlohmann::json read_export(const std::filesystem::path& path);
ImportResult import_into_config(Config& config, const nlohmann::json& data, const std::string& mode,
                                bool import_settings);
std::string format_import_summary(const ImportResult& result, const std::string& mode);

}  // namespace fatty
