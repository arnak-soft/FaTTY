#include "app/whats_new.hpp"

#include "app/version.hpp"
#include "core/util.hpp"

namespace fatty {

std::string release_version_id(std::string_view version) {
  auto text = trim(version);
  if (text.empty()) return {};
  auto [major, minor, patch, extra] = version_tuple(text);
  (void)extra;
  return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

bool should_show_whats_new(std::string_view last_seen, std::string_view current, std::string_view notes) {
  if (trim(notes).empty()) return false;
  auto cur = release_version_id(current);
  if (cur.empty()) return false;
  return release_version_id(last_seen) != cur;
}

}  // namespace fatty
