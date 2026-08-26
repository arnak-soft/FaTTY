#include "core/lockout.hpp"

#include "core/paths.hpp"
#include "core/util.hpp"

#include <chrono>
#include <nlohmann/json.hpp>

namespace fatty {
using json = nlohmann::json;

namespace {

double now_unix() {
  using clock = std::chrono::system_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

bool lockout_enabled(const AppSettings& settings) {
  return settings.master_password_max_attempts > 0;
}

AuthLockoutState normalize(AuthLockoutState state) {
  if (state.locked_until > 0 && now_unix() >= state.locked_until) {
    return {};
  }
  return state;
}

}  // namespace

AuthLockoutState load_lockout() {
  if (!std::filesystem::exists(lockout_path())) {
    return {};
  }
  try {
    json data = json::parse(read_text_file(lockout_path()), nullptr, true, true);
    AuthLockoutState s;
    s.failed_attempts = std::max(0, data.value("failed_attempts", 0));
    s.locked_until = std::max(0.0, data.value("locked_until", 0.0));
    return s;
  } catch (...) {
    return {};
  }
}

void save_lockout(const AuthLockoutState& state) {
  json data = {{"failed_attempts", state.failed_attempts}, {"locked_until", state.locked_until}};
  atomic_write_text(lockout_path(), data.dump(2));
}

int remaining_lockout_seconds(const AppSettings& settings, const AuthLockoutState* state) {
  if (!lockout_enabled(settings)) {
    return 0;
  }
  AuthLockoutState s = normalize(state ? *state : load_lockout());
  if (s.locked_until <= 0) {
    return 0;
  }
  return std::max(0, static_cast<int>(s.locked_until - now_unix()));
}

std::string lockout_message(const AppSettings& settings) {
  int seconds = remaining_lockout_seconds(settings);
  if (seconds <= 0) {
    return "";
  }
  int minutes = seconds / 60;
  int secs = seconds % 60;
  if (minutes) {
    return "Слишком много неудачных попыток. Повторите через " + std::to_string(minutes) + " мин " +
           std::to_string(secs) + " с.";
  }
  return "Слишком много неудачных попыток. Повторите через " + std::to_string(secs) + " с.";
}

std::string record_failed_attempt(const AppSettings& settings) {
  if (!lockout_enabled(settings)) {
    return "Неверный мастер-пароль.";
  }
  auto state = normalize(load_lockout());
  if (remaining_lockout_seconds(settings, &state) > 0) {
    auto msg = lockout_message(settings);
    return msg.empty() ? "Вход временно заблокирован." : msg;
  }
  int max_attempts = std::max(1, settings.master_password_max_attempts);
  state.failed_attempts += 1;
  if (state.failed_attempts >= max_attempts) {
    int minutes = std::max(1, settings.master_password_lockout_minutes);
    state.locked_until = now_unix() + minutes * 60;
    save_lockout(state);
    auto msg = lockout_message(settings);
    return msg.empty() ? "Вход временно заблокирован." : msg;
  }
  save_lockout(state);
  int left = max_attempts - state.failed_attempts;
  return "Неверный мастер-пароль. Осталось попыток: " + std::to_string(left) + ".";
}

void record_success() {
  std::error_code ec;
  if (std::filesystem::exists(lockout_path())) {
    std::filesystem::remove(lockout_path(), ec);
    if (ec) {
      save_lockout({});
    }
  }
}

}  // namespace fatty
