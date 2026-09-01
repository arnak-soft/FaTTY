#include "app/single_instance.hpp"
#include "app/version.hpp"
#include "core/paths.hpp"
#include "core/store.hpp"
#include "core/util.hpp"
#include "ui/app_frame.hpp"
#include "ui/dialogs.hpp"
#include "ui/files_window.hpp"
#include "ui/splash.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <wx/app.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/strconv.h>
#include <wx/timer.h>
#include <wx/window.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#endif

#include <fstream>
#include <cstdlib>
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
    if (const char* smoke = std::getenv("FATTY_SMOKE_TEST"); smoke && smoke[0] == '1') return false;
    for (int i = 1; i < argc; ++i) {
      if (wxString(argv[i]) == L"--smoke-test") return false;
    }
#ifdef _WIN32
    if (wxString(GetCommandLineW()).Contains(L"--smoke-test")) return false;
#endif
    if (!try_become_primary()) {
      activate_existing();
      return false;
    }
    init_install_close_ipc();
    install_close_timer_.SetOwner(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { on_install_close_tick(); });
    install_close_timer_.Start(200);
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
      log_error(std::string("load_config: ") + exc.what());
      hide_splash();
      wxMessageBox(wxString::FromUTF8(exc.what()), wxString::FromUTF8(kAppName), wxOK | wxICON_ERROR);
      install_close_timer_.Stop();
      shutdown_install_close_ipc();
      return false;
    }
    hide_splash();
    set_theme(config.settings.theme);
    MasterPasswordDialog dlg(nullptr, config, vault);
    dlg.setup_layout(&config.settings, "master");
    if (dlg.ShowModal() != wxID_OK || !dlg.ok || !vault.unlocked()) {
      install_close_timer_.Stop();
      shutdown_install_close_ipc();
      return false;
    }
    try {
      save_config(config, vault);
    } catch (const std::exception& exc) {
      wxMessageBox(L"Не удалось сохранить хранилище: " + wxString::FromUTF8(exc.what()),
                   wxString::FromUTF8(kAppName), wxOK | wxICON_ERROR);
      install_close_timer_.Stop();
      shutdown_install_close_ipc();
      return false;
    }
    auto* frame = new AppFrame(std::move(config), std::move(vault));
    frame->Show(true);
    SetTopWindow(frame);
    return true;
  }

  int OnExit() override {
    install_close_timer_.Stop();
    shutdown_install_close_ipc();
    return wxApp::OnExit();
  }

  void OnFatalException() override {
    try {
      std::filesystem::create_directories(app_dir());
      std::ofstream out(error_log_path(), std::ios::binary | std::ios::trunc);
      out << "fatal exception\n";
    } catch (...) {
    }
  }

 private:
  wxTimer install_close_timer_;
  bool closing_for_install_ = false;

  static AppFrame* find_frame() {
    if (auto* frame = dynamic_cast<AppFrame*>(wxTheApp->GetTopWindow())) return frame;
    for (wxWindow* w : wxTopLevelWindows) {
      if (auto* frame = dynamic_cast<AppFrame*>(w)) return frame;
    }
    return nullptr;
  }

  void on_install_close_tick() {
    bool busy = false;
    bool modal = false;
    if (auto* frame = find_frame()) {
      busy = frame->is_busy() || frame->files_busy();
      for (wxWindow* w : wxTopLevelWindows) {
        if (w == frame) continue;
        if (auto* files = dynamic_cast<FilesWindow*>(w); files && files->is_busy()) busy = true;
        if (auto* dlg = wxDynamicCast(w, wxDialog); dlg && dlg->IsModal() && dlg->IsShown()) modal = true;
      }
    }
    publish_install_state(busy, modal);
    if (!take_install_close_request()) return;
    close_for_install();
  }

  void close_for_install() {
    if (closing_for_install_) return;
    closing_for_install_ = true;
    install_close_timer_.Stop();
    if (auto* frame = find_frame()) {
      frame->request_close_for_install();
      return;
    }
    wxWindowList top = wxTopLevelWindows;
    for (wxWindow* w : top) {
      if (auto* dlg = wxDynamicCast(w, wxDialog); dlg && dlg->IsModal()) {
        dlg->EndModal(wxID_CANCEL);
      } else if (w) {
        w->Close(true);
      }
    }
  }
};

}  // namespace fatty

wxIMPLEMENT_APP(fatty::FattyApp);
