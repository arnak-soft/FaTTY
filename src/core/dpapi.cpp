#include "core/dpapi.hpp"

#include "core/util.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

#include <stdexcept>

namespace fatty {

std::string dpapi_unprotect(const std::string& token) {
  if (token.empty()) {
    return "";
  }
#ifdef _WIN32
  auto raw = b64_decode(token);
  DATA_BLOB in{};
  in.cbData = static_cast<DWORD>(raw.size());
  in.pbData = raw.empty() ? nullptr : raw.data();
  DATA_BLOB out{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
    throw std::runtime_error(
        "Не удалось расшифровать пароль. Файл конфига привязан к этой учётной записи Windows.");
  }
  std::string text(reinterpret_cast<char*>(out.pbData), out.cbData);
  if (out.pbData) {
    LocalFree(out.pbData);
  }
  return text;
#else
  throw std::runtime_error("DPAPI доступен только на Windows");
#endif
}

}  // namespace fatty
