#pragma once

#include <string>
#include <string_view>

namespace fatty {

enum class WorkspaceMode {
  Commands = 0,
  Health = 1,
  Backup = 2,
  Files = 3,
};

inline constexpr int kWorkspaceModeCount = 4;

inline const wchar_t* workspace_mode_label(WorkspaceMode mode) {
  switch (mode) {
    case WorkspaceMode::Health:
      return L"Состояние";
    case WorkspaceMode::Backup:
      return L"Резерв";
    case WorkspaceMode::Files:
      return L"Файлы";
    case WorkspaceMode::Commands:
    default:
      return L"Команды";
  }
}

inline const char* workspace_mode_key(WorkspaceMode mode) {
  switch (mode) {
    case WorkspaceMode::Health:
      return "health";
    case WorkspaceMode::Backup:
      return "backup";
    case WorkspaceMode::Files:
      return "files";
    case WorkspaceMode::Commands:
    default:
      return "commands";
  }
}

inline WorkspaceMode workspace_mode_from_key(std::string_view key) {
  if (key == "health") return WorkspaceMode::Health;
  if (key == "backup") return WorkspaceMode::Backup;
  if (key == "files") return WorkspaceMode::Files;
  return WorkspaceMode::Commands;
}

inline WorkspaceMode workspace_mode_from_int(int value) {
  if (value < 0 || value >= kWorkspaceModeCount) return WorkspaceMode::Commands;
  return static_cast<WorkspaceMode>(value);
}

}  // namespace fatty
