#pragma once

#include <string>
#include <string_view>

namespace fatty {

// Пользователю при первом запуске новой версии. 1–4 коротких пункта
// с последнего git-тега. Агент обновляет после каждого заметного изменения.
inline constexpr std::string_view kWhatsNew =
    "• Состояние VPS: шкала Swap рядом с RAM\n"
    "• Нагрузка считается от числа ядер и влияет на статус (жёлтый / красный)\n"
    "• «Запустить» и «Стоп» стоят рядом и не разъезжаются\n"
    "• Открытое меню переключается наведением, как в Windows";

std::string release_version_id(std::string_view version);
bool should_show_whats_new(std::string_view last_seen, std::string_view current,
                           std::string_view notes = kWhatsNew);

}  // namespace fatty
