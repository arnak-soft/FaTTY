#pragma once

#include <string>
#include <string_view>

namespace fatty {

// Пользователю при первом запуске новой версии. 1–4 коротких пункта
// с последнего git-тега. Агент обновляет после каждого заметного изменения.
inline constexpr std::string_view kWhatsNew =
    "• Состояние VPS: Swap, нагрузка, рост диска, reboot и упавшие службы\n"
    "• iowait и steal на шкалах CPU — диск и «шумный сосед»\n"
    "• Кнопка «Состояние» краснеет, если у VPS высокое или нет связи\n"
    "• «Запустить» и «Стоп» рядом; меню переключается наведением";

std::string release_version_id(std::string_view version);
bool should_show_whats_new(std::string_view last_seen, std::string_view current,
                           std::string_view notes = kWhatsNew);

}  // namespace fatty
