#include "core/remote_text.hpp"

#include <cstdint>

namespace fatty {
namespace {

bool utf8_codepoint_ok(std::string_view bytes, std::size_t& i) {
  const auto n = bytes.size();
  if (i >= n) return false;
  const auto b0 = static_cast<unsigned char>(bytes[i]);
  auto need = [&](std::size_t count, unsigned char lo) -> bool {
    if (i + count > n) return false;
    if (b0 < lo) return false;
    for (std::size_t k = 1; k < count; ++k) {
      const auto b = static_cast<unsigned char>(bytes[i + k]);
      if ((b & 0xC0) != 0x80) return false;
    }
    return true;
  };
  if (b0 <= 0x7F) {
    ++i;
    return true;
  }
  if (b0 <= 0xC1) return false;  // invalid / overlong 2-byte
  if (b0 <= 0xDF) {
    if (!need(2, 0xC2)) return false;
    i += 2;
    return true;
  }
  if (b0 <= 0xEF) {
    if (b0 == 0xE0) {
      if (i + 2 >= n) return false;
      const auto b1 = static_cast<unsigned char>(bytes[i + 1]);
      if (b1 < 0xA0 || b1 > 0xBF) return false;
      const auto b2 = static_cast<unsigned char>(bytes[i + 2]);
      if ((b2 & 0xC0) != 0x80) return false;
    } else if (b0 == 0xED) {
      if (i + 2 >= n) return false;
      const auto b1 = static_cast<unsigned char>(bytes[i + 1]);
      if (b1 < 0x80 || b1 > 0x9F) return false;
      const auto b2 = static_cast<unsigned char>(bytes[i + 2]);
      if ((b2 & 0xC0) != 0x80) return false;
    } else {
      if (!need(3, 0xE0)) return false;
    }
    i += 3;
    return true;
  }
  if (b0 <= 0xF4) {
    if (b0 == 0xF0) {
      if (i + 3 >= n) return false;
      const auto b1 = static_cast<unsigned char>(bytes[i + 1]);
      if (b1 < 0x90 || b1 > 0xBF) return false;
      for (std::size_t k = 2; k < 4; ++k) {
        const auto b = static_cast<unsigned char>(bytes[i + k]);
        if ((b & 0xC0) != 0x80) return false;
      }
    } else if (b0 == 0xF4) {
      if (i + 3 >= n) return false;
      const auto b1 = static_cast<unsigned char>(bytes[i + 1]);
      if (b1 < 0x80 || b1 > 0x8F) return false;
      for (std::size_t k = 2; k < 4; ++k) {
        const auto b = static_cast<unsigned char>(bytes[i + k]);
        if ((b & 0xC0) != 0x80) return false;
      }
    } else {
      if (!need(4, 0xF1)) return false;
    }
    i += 4;
    return true;
  }
  return false;
}

bool is_valid_utf8(std::string_view bytes) {
  std::size_t i = 0;
  while (i < bytes.size()) {
    if (!utf8_codepoint_ok(bytes, i)) return false;
  }
  return true;
}

bool has_nul(std::string_view bytes) {
  return bytes.find('\0') != std::string_view::npos;
}

}  // namespace

const char* text_eol_label(TextEolKind eol) {
  switch (eol) {
    case TextEolKind::Crlf:
      return "CRLF";
    case TextEolKind::Cr:
      return "CR";
    case TextEolKind::Lf:
    default:
      return "LF";
  }
}

RemoteTextDecodeResult decode_remote_text(std::string_view bytes, std::size_t max_bytes) {
  RemoteTextDecodeResult out;
  if (bytes.size() > max_bytes) {
    out.error = "Файл больше " + std::to_string(max_bytes / 1024) +
                " КБ — встроенный редактор его не открывает. Скачайте файл.";
    return out;
  }
  if (has_nul(bytes)) {
    out.error = "Похоже на бинарный файл (есть нулевые байты). Открытие отменено.";
    return out;
  }
  if (!is_valid_utf8(bytes)) {
    out.error = "Файл не в UTF-8 или повреждён. Открытие отменено — скачайте и правьте внешне.";
    return out;
  }

  std::size_t lf_only = 0;
  std::size_t cr_only = 0;
  std::size_t crlf = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    const char c = bytes[i];
    if (c == '\r') {
      if (i + 1 < bytes.size() && bytes[i + 1] == '\n') {
        ++crlf;
        ++i;
      } else {
        ++cr_only;
      }
    } else if (c == '\n') {
      ++lf_only;
    }
  }

  const int kinds = (lf_only > 0 ? 1 : 0) + (cr_only > 0 ? 1 : 0) + (crlf > 0 ? 1 : 0);
  if (kinds > 1) {
    out.error =
        "В файле смешаны переводы строк (LF/CRLF/CR). Так сохранять опасно — скачайте файл "
        "или выровняйте переводы во внешнем редакторе.";
    return out;
  }

  TextEolKind eol = TextEolKind::Lf;
  if (crlf > 0) eol = TextEolKind::Crlf;
  else if (cr_only > 0) eol = TextEolKind::Cr;

  std::string display;
  display.reserve(bytes.size());
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    const char c = bytes[i];
    if (c == '\r') {
      if (i + 1 < bytes.size() && bytes[i + 1] == '\n') ++i;
      display.push_back('\n');
    } else {
      display.push_back(c);
    }
  }

  out.ok = true;
  out.view.display_utf8 = std::move(display);
  out.view.eol = eol;
  return out;
}

std::string normalize_editor_newlines(std::string_view utf8) {
  std::string out;
  out.reserve(utf8.size());
  for (std::size_t i = 0; i < utf8.size(); ++i) {
    const char c = utf8[i];
    if (c == '\r') {
      if (i + 1 < utf8.size() && utf8[i + 1] == '\n') ++i;
      out.push_back('\n');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string encode_remote_text(std::string_view display_utf8_lf, TextEolKind eol) {
  const std::string lf = normalize_editor_newlines(display_utf8_lf);
  if (eol == TextEolKind::Lf) return lf;
  std::string out;
  out.reserve(lf.size() + (eol == TextEolKind::Crlf ? lf.size() / 8 + 8 : 0));
  for (char c : lf) {
    if (c == '\n') {
      if (eol == TextEolKind::Crlf) {
        out.push_back('\r');
        out.push_back('\n');
      } else {
        out.push_back('\r');
      }
    } else {
      out.push_back(c);
    }
  }
  return out;
}

}  // namespace fatty
