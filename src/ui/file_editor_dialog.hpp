#pragma once

#include "core/remote_text.hpp"
#include "ui/layout.hpp"

#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <string>

namespace fatty {

/// Простой текстовый редактор удалённого файла. Байты на диск/SFTP не пишет —
/// возвращает UTF-8 с исходным стилем EOL после «Сохранить».
class FileEditorDialog : public PositionedDialog {
 public:
  FileEditorDialog(wxWindow* parent, const wxString& title, RemoteTextView view);

  bool saved = false;
  /// Готовые байты для `write_file` (с исходным LF/CRLF/CR).
  std::string result_bytes;

 private:
  void update_meta();
  void on_save();
  void on_close(wxCloseEvent& e);
  bool confirm_discard();

  RemoteTextView view_;
  wxTextCtrl* text_{};
  wxStaticText* meta_{};
  bool dirty_ = false;
};

}  // namespace fatty
