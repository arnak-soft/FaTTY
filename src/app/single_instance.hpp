#pragma once

namespace fatty {
bool try_become_primary();
void register_window(void* hwnd);
bool activate_existing();
}  // namespace fatty
