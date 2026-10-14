#pragma once

#include <string>
#include <string_view>

namespace fatty {

// Пользователю при первом запуске новой версии. 1–4 коротких пункта
// с последнего git-тега. Агент обновляет после каждого заметного изменения.
inline constexpr std::string_view kWhatsNew =
    "• Состояние VPS: Swap, Docker, OOM, reboot, службы, iowait/steal\n"
    "• На карточке — рост RAM/диска и мини-график между проверками\n"
    "• Смена статуса VPS пишется в журнал (Ctrl+J)\n"
    "• Кнопка «Состояние» краснеет при высоком или нет связи";

std::string release_version_id(std::string_view version);
bool should_show_whats_new(std::string_view last_seen, std::string_view current,
                           std::string_view notes = kWhatsNew);

}  // namespace fatty
