#include "ui/command_transfer.hpp"

#include "core/util.hpp"

#include <wx/choicdlg.h>
#include <wx/msgdlg.h>

#include <map>
#include <set>

namespace fatty {
namespace {

wxArrayString group_labels(const Config& config, const std::string& server_id, std::vector<std::string>* ids) {
  wxArrayString labels;
  ids->clear();
  labels.Add(L"Общее");
  ids->push_back("");
  for (const auto& g : config.groups_for(server_id)) {
    labels.Add(wxString::FromUTF8(g.name));
    ids->push_back(g.id);
  }
  return labels;
}

std::vector<std::string> command_names_on(const Config& config, const std::string& server_id) {
  std::vector<std::string> names;
  for (const auto& c : config.commands_for(server_id)) names.push_back(c.name);
  return names;
}

}  // namespace

std::optional<std::string> pick_group_id(wxWindow* parent, const Config& config, const std::string& server_id,
                                         const wxString& title, const wxString& prompt) {
  std::vector<std::string> ids;
  auto labels = group_labels(config, server_id, &ids);
  const int n = wxGetSingleChoiceIndex(prompt, title, labels, parent);
  if (n < 0 || n >= static_cast<int>(ids.size())) return std::nullopt;
  return ids[static_cast<std::size_t>(n)];
}

std::optional<TransferTarget> pick_server_and_group(wxWindow* parent, const Config& config,
                                                    const std::string& exclude_server_id, const wxString& title) {
  wxArrayString server_labels;
  std::vector<std::string> server_ids;
  for (const auto& s : config.servers) {
    if (!exclude_server_id.empty() && s.id == exclude_server_id) continue;
    server_labels.Add(wxString::FromUTF8(s.name.empty() ? s.host : s.name));
    server_ids.push_back(s.id);
  }
  if (server_ids.empty()) {
    wxMessageBox(L"Нет другого VPS для выбора.", title, wxOK | wxICON_INFORMATION, parent);
    return std::nullopt;
  }
  const int sn = wxGetSingleChoiceIndex(L"Выберите VPS", title, server_labels, parent);
  if (sn < 0 || sn >= static_cast<int>(server_ids.size())) return std::nullopt;
  TransferTarget dest;
  dest.server_id = server_ids[static_cast<std::size_t>(sn)];
  auto group = pick_group_id(parent, config, dest.server_id, title, L"В какую группу положить?");
  if (!group) return std::nullopt;
  dest.group_id = *group;
  return dest;
}

int copy_commands_to_group(Config& config, const std::vector<Command*>& cmds, const std::string& group_id) {
  int n = 0;
  for (auto* c : cmds) {
    if (!c) continue;
    auto names = command_names_on(config, c->server_id);
    auto clone = c->duplicate(copy_name(c->name, names));
    clone.group_id = group_id;
    config.commands.push_back(std::move(clone));
    ++n;
  }
  return n;
}

int copy_commands_to_vps(Config& config, const std::vector<Command*>& cmds, const TransferTarget& dest) {
  if (!config.server_by_id(dest.server_id)) return 0;
  if (!dest.group_id.empty() && !config.group_by_id(dest.group_id)) return 0;
  int n = 0;
  auto names = command_names_on(config, dest.server_id);
  for (auto* c : cmds) {
    if (!c) continue;
    auto clone = c->duplicate(copy_name(c->name, names), dest.server_id);
    clone.group_id = dest.group_id;
    names.push_back(clone.name);
    config.commands.push_back(std::move(clone));
    ++n;
  }
  return n;
}

int move_commands_to_group(Config& config, const std::vector<Command*>& cmds, const std::string& server_id,
                           const std::string& group_id) {
  int n = 0;
  for (auto* c : cmds) {
    if (!c || c->server_id != server_id) continue;
    if (c->group_id == group_id) continue;
    c->group_id = group_id;
    ++n;
  }
  return n;
}

int move_commands_to_vps(Config& config, const std::vector<Command*>& cmds, const TransferTarget& dest) {
  if (!config.server_by_id(dest.server_id)) return 0;
  if (!dest.group_id.empty() && !config.group_by_id(dest.group_id)) return 0;
  int n = 0;
  for (auto* c : cmds) {
    if (!c) continue;
    if (c->server_id == dest.server_id && c->group_id == dest.group_id) continue;
    if (c->server_id != dest.server_id) {
      config.drop_command_from_bundles(c->id);
    }
    c->server_id = dest.server_id;
    c->group_id = dest.group_id;
    ++n;
  }
  return n;
}

bool copy_bundle_to_vps(Config& config, const Bundle& bundle, const std::string& dest_server_id,
                        std::string* error) {
  if (!config.server_by_id(dest_server_id)) {
    if (error) *error = "VPS не найден";
    return false;
  }
  if (dest_server_id == bundle.server_id) {
    if (error) *error = "Выберите другой VPS";
    return false;
  }

  std::map<std::string, std::string> group_map;
  std::set<std::string> needed_groups;
  for (const auto& cid : bundle.command_ids) {
    auto* c = config.command_by_id(cid);
    if (!c || c->group_id.empty()) continue;
    needed_groups.insert(c->group_id);
  }
  for (const auto& gid : needed_groups) {
    auto* g = config.group_by_id(gid);
    if (!g) continue;
    // Ищем одноимённую группу на целевом VPS или создаём.
    std::string matched;
    for (const auto& tg : config.groups_for(dest_server_id)) {
      if (tg.name == g->name) {
        matched = tg.id;
        break;
      }
    }
    if (matched.empty()) {
      auto ng = CommandGroup::make_new(dest_server_id, g->name);
      ng.working_dir = g->working_dir;
      matched = ng.id;
      config.groups.push_back(std::move(ng));
    }
    group_map[gid] = matched;
  }

  auto names = command_names_on(config, dest_server_id);
  std::map<std::string, std::string> cmd_map;
  for (const auto& cid : bundle.command_ids) {
    auto* c = config.command_by_id(cid);
    if (!c) continue;
    auto clone = c->duplicate(copy_name(c->name, names), dest_server_id);
    if (!c->group_id.empty() && group_map.count(c->group_id)) {
      clone.group_id = group_map[c->group_id];
    } else {
      clone.group_id.clear();
    }
    names.push_back(clone.name);
    cmd_map[cid] = clone.id;
    config.commands.push_back(std::move(clone));
  }

  std::vector<std::string> bundle_names;
  for (const auto& b : config.bundles_for(dest_server_id)) bundle_names.push_back(b.name);
  auto nb = Bundle::make_new(dest_server_id);
  nb.name = copy_name(bundle.name, bundle_names);
  nb.interval_sec = bundle.interval_sec;
  for (const auto& cid : bundle.command_ids) {
    auto it = cmd_map.find(cid);
    if (it != cmd_map.end()) nb.command_ids.push_back(it->second);
  }
  if (nb.command_ids.empty()) {
    if (error) *error = "В связке нет команд для копирования";
    return false;
  }
  config.bundles.push_back(std::move(nb));
  return true;
}

}  // namespace fatty
