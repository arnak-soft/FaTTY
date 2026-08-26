#pragma once

#include "core/store.hpp"
#include "net/sftp_session.hpp"

#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <atomic>
#include <string>
#include <vector>

namespace fatty {

class FilesWindow : public wxFrame {
 public:
  FilesWindow(wxWindow* parent, const Server& server, std::string start_path);
  ~FilesWindow() override;

 private:
  void connect();
  void refresh();
  void go_up();
  void download_selected();
  void upload();
  Server server_;
  std::string start_path_;
  SFTPSession session_;
  std::vector<RemoteEntry> entries_;
  wxTextCtrl* path_{};
  wxListCtrl* list_{};
  wxGauge* gauge_{};
  wxStaticText* status_{};
  std::atomic<bool> busy_{false};
};

}  // namespace fatty
