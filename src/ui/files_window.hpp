#pragma once

#include "core/store.hpp"
#include "net/sftp_session.hpp"
#include "ui/chrome.hpp"

#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fatty {

/// Панель SFTP для одного VPS (встраивается; у каждого VPS своя).
class FilesWindow : public wxPanel {
 public:
  FilesWindow(wxWindow* parent, const Server& server, std::string start_path, AppSettings* settings = nullptr,
              std::function<void()> persist = {});
  ~FilesWindow() override;
  bool is_busy() const { return busy_.load(); }
  const std::string& server_id() const { return server_.id; }

 private:
  void connect();
  void refresh();
  void go_up();
  void download_selected();
  void upload();
  void set_busy(bool busy);
  Server server_;
  std::string start_path_;
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
