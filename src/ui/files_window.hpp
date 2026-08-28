#pragma once

#include "core/store.hpp"
#include "net/sftp_session.hpp"
#include "ui/chrome.hpp"

#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace fatty {

class FilesWindow : public wxFrame {
 public:
  FilesWindow(wxWindow* parent, const Server& server, std::string start_path);
  ~FilesWindow() override;
  bool is_busy() const { return busy_.load(); }

 private:
  void connect();
  void refresh();
  void go_up();
  void download_selected();
  void upload();
  void set_busy(bool busy);
  Server server_;
  std::string start_path_;
  // shared_ptr, чтобы воркер-поток удерживал сессию живой, даже если окно закрыли
  // до конца передачи/подключения.
  std::shared_ptr<SFTPSession> session_ = std::make_shared<SFTPSession>();
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  std::vector<RemoteEntry> entries_;
  wxTextCtrl* path_{};
  wxListCtrl* list_{};
  wxGauge* gauge_{};
  wxStaticText* status_{};
  RoundButton* stop_btn_{};
  std::vector<wxWindow*> busy_disable_;
  std::atomic<bool> busy_{false};
};

}  // namespace fatty
