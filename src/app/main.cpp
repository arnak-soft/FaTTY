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
#include <wx/strconv.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#endif

#include <fstream>
#include <nlohmann/json.hpp>

namespace fatty {
namespace {

// MSWEnableDarkMode должен быть вызван до создания первого окна, т.е. до
// полного load_config(). Читаем только тему, молча падая обратно на тёмную.
std::string peek_theme() {
  try {
    std::ifstream in(config_path(), std::ios::binary);
    if (!in) return "dark";
    auto data = nlohmann::json::parse(in, nullptr, false);
    if (data.is_discarded()) return "dark";
    auto theme = data.value("settings", nlohmann::json::object()).value("theme", std::string("dark"));
    return theme == "light" ? "light" : "dark";
  } catch (...) {
    return "dark";
  }
}

}  // namespace

class FattyApp : public wxApp {
 public:
  bool Initialize(int& argc, wxChar** argv) override {
    // Задаёт кодировку для обратного направления, wxString -> narrow (mbc_str
    // и т.п.). На wxString(const char*) не влияет: там жёстко зашит wxConvLibc,
    // то есть ANSI-кодовая страница. От крякозябр защищает не эта строка, а
    // wxNO_IMPLICIT_WXSTRING_ENCODING (см. CMakeLists.txt): текст UI — L"…",
    // UTF-8 std::string из core/net — только через wxString::FromUTF8().
    wxConvCurrent = &wxConvUTF8;
    return wxApp::Initialize(argc, argv);
  }

  bool OnInit() override {
#ifdef _WIN32
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetCurrentProcessExplicitAppUserModelID(L"FaTTY");
#endif
    if (!try_become_primary()) {
      activate_existing();
      return false;
    }
    set_theme(peek_theme());
#ifdef __WXMSW__
    if (theme_is_dark()) MSWEnableDarkMode(DarkMode_Always);
#endif
    wxInitAllImageHandlers();
    show_splash();
    Config config;
    SessionVault vault;
    try {
      config = load_config();
    } catch (const std::exception& exc) {
      hide_splash();
      wxMessageBox(wxString::FromUTF8(exc.what()), wxString::FromUTF8(kAppName), wxOK | wxICON_ERROR);
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
      wxMessageBox(L"Не удалось сохранить хранилище: " + wxString::FromUTF8(exc.what()),
                   wxString::FromUTF8(kAppName), wxOK | wxICON_ERROR);
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
