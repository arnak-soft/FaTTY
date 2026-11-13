#pragma once

#include <string>
#include <string_view>

namespace fatty {

// Пользователю при первом запуске новой версии. 1–4 коротких пункта
// с последнего git-тега. Агент обновляет после каждого заметного изменения.
inline constexpr std::string_view kWhatsNew =
    "• Резерв: колонка «Последний раз» и отдельный полный журнал запусков\n"
    "• Цвет VPS: своя палитра из 14 оттенков вместо окна Windows\n"
    "• Новый VPS сразу получает свой цвет — можно сбросить в карточке";

std::string release_version_id(std::string_view version);
bool should_show_whats_new(std::string_view last_seen, std::string_view current,
                           std::string_view notes = kWhatsNew);

}  // namespace fatty
