#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace fatty {

/// Потолок для правки в FaTTY: целиком в память + wxTextCtrl.
inline constexpr std::size_t kRemoteTextMaxBytes = 512 * 1024;

enum class TextEolKind { Lf, Crlf, Cr };

struct RemoteTextView {
  /// Текст для редактора: только UTF-8, переводы строк нормализованы в `\n`.
  std::string display_utf8;
  TextEolKind eol = TextEolKind::Lf;
};

struct RemoteTextDecodeResult {
  bool ok = false;
  std::string error;
  RemoteTextView view;
};

/// Строгая проверка: размер, NUL, UTF-8, единый стиль переводов строк (без смеси).
RemoteTextDecodeResult decode_remote_text(std::string_view bytes,
                                          std::size_t max_bytes = kRemoteTextMaxBytes);

/// `display_utf8` — UTF-8 с `\n` (как после нормализации из редактора).
std::string encode_remote_text(std::string_view display_utf8_lf, TextEolKind eol);

/// Приводит то, что вернул wx (возможны `\r\n` / `\r`), к UTF-8 с одними `\n`.
std::string normalize_editor_newlines(std::string_view utf8);

const char* text_eol_label(TextEolKind eol);

}  // namespace fatty
