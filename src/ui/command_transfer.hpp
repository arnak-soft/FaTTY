#pragma once

#include "core/store.hpp"

#include <wx/window.h>

#include <optional>
#include <string>
#include <vector>

namespace fatty {

struct TransferTarget {
  std::string server_id;
  std::string group_id;  // пусто = «Общее»
};

/// Выбор группы на уже известном VPS. nullopt — отмена.
std::optional<std::string> pick_group_id(wxWindow* parent, const Config& config, const std::string& server_id,
                                         const wxString& title, const wxString& prompt);

/// Выбор VPS и группы. exclude_server_id — скрыть текущий (для «на другой VPS»).
std::optional<TransferTarget> pick_server_and_group(wxWindow* parent, const Config& config,
                                                    const std::string& exclude_server_id, const wxString& title);

/// Копии в другую группу того же VPS. Возвращает число созданных.
int copy_commands_to_group(Config& config, const std::vector<Command*>& cmds, const std::string& group_id);

/// Копии на другой VPS (и группу). Имена уникализируются на целевом сервере.
int copy_commands_to_vps(Config& config, const std::vector<Command*>& cmds, const TransferTarget& dest);

/// Перенос в группу того же VPS.
int move_commands_to_group(Config& config, const std::vector<Command*>& cmds, const std::string& server_id,
                           const std::string& group_id);

/// Перенос на другой VPS: меняет server_id/group_id, выкидывает из связок исходного VPS.
int move_commands_to_vps(Config& config, const std::vector<Command*>& cmds, const TransferTarget& dest);

/// Копия связки на другой VPS: клонирует недостающие команды и саму связку.
bool copy_bundle_to_vps(Config& config, const Bundle& bundle, const std::string& dest_server_id,
                        std::string* error = nullptr);

}  // namespace fatty
