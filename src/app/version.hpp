#pragma once

#include <string>
#include <tuple>

namespace fatty {

std::string resolve_version();
std::tuple<int, int, int, int> version_tuple(const std::string& version = {});

}  // namespace fatty
