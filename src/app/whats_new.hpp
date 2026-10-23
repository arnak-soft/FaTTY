#pragma once

#include <string>
#include <string_view>

namespace fatty {

// Пользователю при первом запуске новой версии. 1–4 коротких пункта
// с последнего git-тега. Агент обновляет после каждого заметного изменения.
inline constexpr std::string_view kWhatsNew =
    "• Цвет VPS тонирует всё главное окно (отдельно для тёмной и светлой темы)\n"
    "• Связки «по шагам» — в отдельных окнах; у VPS свой шрифт списков\n"
    "• В «по шагам» подписи шага и длинная команда отображаются без наложений";

std::string release_version_id(std::string_view version);
bool should_show_whats_new(std::string_view last_seen, std::string_view current,
                           std::string_view notes = kWhatsNew);

}  // namespace fatty
