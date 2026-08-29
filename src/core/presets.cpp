#include "core/presets.hpp"

namespace fatty {

std::vector<Preset> deploy_presets(std::string app, std::string branch, std::string pm2) {
  if (app.empty()) app = kDefaultAppDir;
  while (!app.empty() && app.back() == '/') app.pop_back();
  if (branch.empty()) branch = kDefaultBranch;
  if (pm2.empty()) pm2 = kDefaultPm2;
  return {
      {"Deploy", "cd " + app + " && git pull origin " + branch + " && pm2 restart " + pm2, 300, true,
       "git pull и перезапуск pm2"},
      {"Git pull", "cd " + app + " && git pull origin " + branch, 180, true, "подтянуть ветку без перезапуска"},
      {"PM2 restart", "pm2 restart " + pm2, 60, true, "перезапустить процесс"},
      {"PM2 status", "pm2 status", 30, true, "список процессов"},
      {"PM2 logs", "pm2 logs " + pm2 + " --lines 120 --nostream", 30, true, "последние 120 строк, без follow"},
      {"Git status", "cd " + app + " && git status -sb && echo && git log -8 --oneline", 30, true,
       "ветка и последние коммиты"},
  };
}

std::vector<Preset> server_presets() {
  return {
      {"Состояние сервера", "hostname; date; uptime; echo; df -hT; echo; free -h", 30, true, "диск, память, uptime"},
      {"Nginx reload", "nginx -t && (systemctl reload nginx || service nginx reload)", 30, true,
       "проверка конфига и reload"},
      {"Nginx status", "systemctl status nginx --no-pager -l || service nginx status", 30, true, "статус сервиса"},
  };
}

std::vector<Preset> all_presets(const std::string& app_dir, const std::string& branch, const std::string& pm2_name,
                                bool include_server) {
  auto items = deploy_presets(app_dir, branch, pm2_name);
  if (include_server) {
    auto extra = server_presets();
    items.insert(items.end(), extra.begin(), extra.end());
  }
  return items;
}

}  // namespace fatty
