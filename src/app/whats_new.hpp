#pragma once

#include <string>
#include <string_view>

namespace fatty {

// Пользователю при первом запуске новой версии. 1–4 коротких пункта
// с последнего git-тега. Агент обновляет после каждого заметного изменения.
inline constexpr std::string_view kWhatsNew =
    "• Вкладка Shell: прокрутка истории; вывод команд F5 включён сразу\n"
    "• Группы команд и связки — по шагам или автозапуск с паузой\n"
    "• Окно «Состояние VPS»: CPU, RAM и диск\n"
    "• После обновления коротко показывается, что нового";

std::string release_version_id(std::string_view version);
bool should_show_whats_new(std::string_view last_seen, std::string_view current,
                           std::string_view notes = kWhatsNew);

}  // namespace fatty
