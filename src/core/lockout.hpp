#pragma once

#include "core/store.hpp"

#include <string>

namespace fatty {

struct AuthLockoutState {
  int failed_attempts = 0;
  double locked_until = 0.0;
};

AuthLockoutState load_lockout();
void save_lockout(const AuthLockoutState& state);
int remaining_lockout_seconds(const AppSettings& settings, const AuthLockoutState* state = nullptr);
std::string lockout_message(const AppSettings& settings);
std::string record_failed_attempt(const AppSettings& settings);
void record_success();

}  // namespace fatty
