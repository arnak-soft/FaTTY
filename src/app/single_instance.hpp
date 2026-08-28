#pragma once

namespace fatty {
bool try_become_primary();
void register_window(void* hwnd);
bool activate_existing();

// IPC с установщиком (Inno Setup): именованные события в Local\.
// CloseForInstall — установщик просит закрыться; BusyWork / OpenDialog —
// текущее состояние, чтобы установщик спросил подтверждение, если нужно.
void init_install_close_ipc();
void shutdown_install_close_ipc();
bool take_install_close_request();
void publish_install_state(bool busy_work, bool open_dialog);
}  // namespace fatty
