#pragma once

#include <string>

class wxWindow;

namespace fatty {

/// Короткий toast поверх других окон без захвата фокуса (не системный Action Center).
/// Показывать, когда пользователь не в FaTTY.
void show_completion_toast(wxWindow* main_frame, const std::string& title, const std::string& detail,
                           const std::string& status);

/// true, если любое окно этого процесса сейчас на переднем плане.
bool app_is_foreground();

}  // namespace fatty
