#include "app/single_instance.hpp"
#include "app/version.hpp"
#include "core/paths.hpp"
#include "core/store.hpp"
#include "core/util.hpp"
#include "ui/app_frame.hpp"
#include "ui/dialogs.hpp"
#include "ui/splash.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/app.h>
#include <wx/msgdlg.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#endif

#include <fstream>

namespace fatty {

class FattyApp : public wxApp {
 public:
  bool OnInit() override {
#ifdef _WIN32
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetCurrentProcessExplicitAppUserModelID(L"FaTTY");
#endif
    if (!try_become_primary()) {
      activate_existing();
      return false;
    }
    wxInitAllImageHandlers();
    show_splash();
    Config config;
    SessionVault vault;
    try {
      config = load_config();
    } catch (const std::exception& exc) {
      hide_splash();
      wxMessageBox(wxString::FromUTF8(exc.what()), kAppName, wxOK | wxICON_ERROR);
      return false;
    }
    hide_splash();
    set_theme(config.settings.theme);
    MasterPasswordDialog dlg(nullptr, config, vault);
    dlg.setup_layout(&config.settings, "master");
    if (dlg.ShowModal() != wxID_OK || !dlg.ok || !vault.unlocked()) {
      return false;
    }
    try {
      save_config(config, vault);
    } catch (const std::exception& exc) {
      wxMessageBox(wxString::FromUTF8(std::string("Не удалось сохранить хранилище: ") + exc.what()), kAppName,
                   wxOK | wxICON_ERROR);
      return false;
    }
    auto* frame = new AppFrame(std::move(config), std::move(vault));
    frame->Show(true);
    SetTopWindow(frame);
    return true;
  }

  void OnFatalException() override {
    try {
      std::filesystem::create_directories(app_dir());
      std::ofstream out(error_log_path(), std::ios::binary | std::ios::trunc);
      out << "fatal exception\n";
    } catch (...) {
    }
  }
};

}  // namespace fatty

wxIMPLEMENT_APP(fatty::FattyApp);
