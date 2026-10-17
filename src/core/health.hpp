#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fatty {

inline constexpr int kHealthIntervalMin = 300;
inline constexpr int kHealthIntervalMax = 30 * 24 * 3600;
inline constexpr int kHealthIntervalDefault = 24 * 3600;
inline constexpr int kHealthTimeoutMin = 5;
inline constexpr int kHealthTimeoutMax = 120;
inline constexpr int kHealthTimeoutDefault = 20;
/// Сколько прошлых замеров хранить на VPS (без текущего).
inline constexpr int kHealthHistoryMax = 48;
/// Сколько имён упавших юнитов systemd сохранять в снимке.
inline constexpr int kHealthFailedUnitsMax = 8;
/// Сколько имён контейнеров Docker (exited / unhealthy) сохранять.
inline constexpr int kHealthDockerNamesMax = 6;
/// Топ процессов по RAM / CPU в снимке.
inline constexpr int kHealthTopProcsMax = 5;
/// Сбойные SSH-логины за сутки: пороги статуса.
inline constexpr int kHealthSshFailWarn = 20;
inline constexpr int kHealthSshFailCrit = 100;
/// Рост сетевых ошибок/дропов с прошлой проверки.
inline constexpr int kHealthNetErrWarn = 1;
inline constexpr int kHealthNetErrCrit = 50;
/// |смещение часов|, секунды.
inline constexpr double kHealthTimeOffsetWarnSec = 5.0;
inline constexpr double kHealthTimeOffsetCritSec = 60.0;
/// Точек на мини-графике карточки (включая текущий замер).
inline constexpr int kHealthSparkMax = 12;

enum class HealthLevel {
  Unknown = 0,
  Ok,
  Warn,
  Crit,
  Offline,
  Unsupported,
  Checking,
};

struct HealthDisk {
  std::string mount;
  long long used_kb = 0;
  long long total_kb = 0;
  double pct() const;
};

struct HealthProc {
  int pid = 0;
  long long rss_kb = 0;
  double cpu_pct = -1;
  std::string name;
};

struct HealthThresholds {
  int disk_warn = 80;
  int disk_crit = 90;
  int ram_warn = 80;
  int ram_crit = 90;
  int swap_warn = 50;
  int swap_crit = 80;
  int cpu_warn = 80;
  int cpu_crit = 95;
  int iowait_warn = 25;
  int iowait_crit = 50;
  int steal_warn = 15;
  int steal_crit = 30;
  // Нагрузка как % от числа ядер (load / nproc * 100). Может быть > 100.
  int load_warn = 100;
  int load_crit = 150;
  int ssh_warn = kHealthSshFailWarn;
  int ssh_crit = kHealthSshFailCrit;
};

/// Какие тревоги не влияют на статус (цифры в деталях всё равно видны).
struct HealthAlertIgnore {
  bool ssh = false;
  bool oom = false;
  bool failed_units = false;
  bool reboot = false;
  bool docker = false;
  bool time = false;
  bool net = false;
};

struct HealthCollect {
  bool cpu = true;
  bool ram = true;
  bool disk = true;
  bool load = true;
  bool docker_disks = false;
  bool swap = true;
  bool docker = false;
};

struct HealthSnapshot {
  std::string server_id;
  std::string server_name;
  HealthLevel level = HealthLevel::Unknown;
  bool checking = false;
  std::string error;
  int nproc = 0;
  double load1 = -1;
  double load5 = -1;
  double load15 = -1;
  double cpu_pct = -1;
  double iowait_pct = -1;
  double steal_pct = -1;
  long long mem_total_kb = 0;
  long long mem_avail_kb = 0;
  double mem_pct = -1;
  long long swap_total_kb = 0;
  long long swap_free_kb = 0;
  double swap_pct = -1;
  double uptime_sec = -1;
  bool reboot_required = false;
  /// Аптайм короче, чем ожидалось после прошлого замера — машина перезагружалась.
  bool rebooted = false;
  std::vector<std::string> failed_units;
  /// true, если `docker` на сервере ответил (есть бинарь и права).
  bool docker_present = false;
  int docker_running = -1;
  int docker_exited = -1;
  int docker_unhealthy = -1;
  std::vector<std::string> docker_exited_names;
  std::vector<std::string> docker_unhealthy_names;
  /// Число OOM-убийств в логах (−1 = не собирали).
  int oom_count = -1;
  std::string oom_last;
  /// Сумма по не-lo интерфейсам из /proc/net/dev (−1 = нет данных).
  long long net_rx_bytes = -1;
  long long net_tx_bytes = -1;
  long long net_rx_errs = -1;
  long long net_tx_errs = -1;
  long long net_rx_drop = -1;
  long long net_tx_drop = -1;
  std::string net_if;
  /// Рост errs+drop с прошлой проверки (−1 = нет базы для сравнения).
  long long net_err_delta = -1;
  /// Сбойные SSH-попытки за ~24 ч (−1 = не собирали).
  int ssh_fail_count = -1;
  /// NTP/chrony: −1 неизвестно, 0 нет, 1 да.
  int time_synced = -1;
  bool time_offset_known = false;
  /// Смещение системных часов, сек (знак как у chrony).
  double time_offset_sec = 0;
  std::vector<HealthProc> top_mem;
  std::vector<HealthProc> top_cpu;
  std::vector<HealthDisk> disks;
  double checked_at = 0;
};

struct HealthCacheEntry {
  HealthSnapshot latest;
  /// Прошлые замеры, от новых к старым. Текущий `latest` сюда не входит.
  std::vector<HealthSnapshot> history;
};

struct HealthTrend {
  bool has_prev = false;
  double prev_checked_at = 0;
  bool mem = false;
  long long mem_used_delta_kb = 0;
  bool swap = false;
  long long swap_used_delta_kb = 0;
  bool disk = false;
  std::string disk_mount;
  long long disk_used_delta_kb = 0;
  double disk_pct_delta = 0;
  bool net = false;
  long long net_rx_delta = 0;
  long long net_tx_delta = 0;
  long long net_err_delta = 0;
  double net_rx_bps = -1;
  double net_tx_bps = -1;
};

HealthLevel worse_health(HealthLevel a, HealthLevel b);
HealthLevel level_from_pct(double pct, int warn, int crit);
/// load15 (иначе load5 / load1) / nproc * 100; −1 если нет данных.
double health_load_pct(const HealthSnapshot& snap);
void apply_health_thresholds(HealthSnapshot& snap, const HealthThresholds& thresholds,
                             const HealthAlertIgnore& ignore = {});

HealthSnapshot parse_health_output(std::string_view text);
std::string health_remote_script(const HealthCollect& collect);

bool health_is_due(const HealthSnapshot& snap, int interval_sec, double now_unix);
double unix_now();

const HealthDisk* health_root_or_worst(const HealthSnapshot& snap);
bool health_mount_is_virtual(std::string_view mount);
void health_apply_virtual_disks(HealthSnapshot& snap, bool show_virtual, const HealthThresholds& thresholds,
                                const HealthAlertIgnore& ignore = {});

std::string health_level_id(HealthLevel level);
HealthLevel health_level_from_id(std::string_view id);
std::string health_level_label(HealthLevel level);
/// Короткие причины статуса «средне» / «высокое» (CPU 92%, OOM ×3, …).
std::vector<std::string> health_alert_reasons(const HealthSnapshot& snap, const HealthThresholds& thresholds,
                                              const HealthAlertIgnore& ignore = {});
/// До max_items причин через « · », остаток как «+N».
std::string health_alert_summary(const HealthSnapshot& snap, const HealthThresholds& thresholds, int max_items = 3,
                                 const HealthAlertIgnore& ignore = {});

/// Краткий индикатор для списка VPS: цветной кубик + текст подсказки.
struct HealthListBadge {
  std::string tip;
  HealthLevel level = HealthLevel::Unknown;
};
/// CPU, RAM, диск, нагрузка — четыре кубика; при офлайне/нет данных все одного тона.
std::vector<HealthListBadge> health_list_badges(const HealthSnapshot& snap, const HealthThresholds& thresholds,
                                                bool enabled);

std::string format_kib(long long kb);
std::string format_pct(double pct);
std::string format_signed_kib(long long delta_kb);
std::string format_signed_pct(double delta_pct);
std::string format_uptime_sec(double seconds);
std::string format_health_when(double unix_ts, double now_unix = 0);
std::string format_health_span(double older_ts, double newer_ts);
std::string format_interval_label(int seconds);

long long health_mem_used_kb(const HealthSnapshot& snap);
long long health_swap_used_kb(const HealthSnapshot& snap);
long long health_net_errs(const HealthSnapshot& snap);
HealthTrend health_trend(const HealthSnapshot& current, const HealthSnapshot& previous);
/// Учесть рост сетевых ошибок относительно прошлого снимка (пишет net_err_delta и усиливает level).
void health_apply_net_delta(HealthSnapshot& snap, const HealthSnapshot& previous,
                            const HealthAlertIgnore& ignore = {});
/// true, если аптайм не согласуется с прошлым замером (перезагрузка между проверками).
bool health_rebooted_since(const HealthSnapshot& current, const HealthSnapshot& previous);
void health_history_push(HealthCacheEntry& entry, HealthSnapshot previous, int max_keep = kHealthHistoryMax);
std::string format_bytes(long long bytes);
std::string format_bps(double bytes_per_sec);
/// Серия pct от старых к новым (latest в конце), для мини-графика.
std::vector<double> health_spark_mem_pct(const HealthCacheEntry& entry, int max_points = kHealthSparkMax);
std::vector<double> health_spark_disk_pct(const HealthCacheEntry& entry, int max_points = kHealthSparkMax);
bool health_level_is_alert(HealthLevel level);

struct HealthFleetAlert {
  int crit = 0;
  int offline = 0;
  int total() const { return crit + offline; }
  HealthLevel worst() const {
    if (offline > 0 || crit > 0) return offline > 0 ? HealthLevel::Offline : HealthLevel::Crit;
    return HealthLevel::Unknown;
  }
};
/// Сколько включённых в проверку VPS в «высокое» / «нет связи».
HealthFleetAlert health_fleet_alert(const std::map<std::string, HealthSnapshot>& snaps,
                                    const std::vector<std::string>& enabled_ids);

std::map<std::string, HealthCacheEntry> load_health_cache(const std::filesystem::path& path);
void save_health_cache(const std::filesystem::path& path, const std::map<std::string, HealthCacheEntry>& entries);

}  // namespace fatty
