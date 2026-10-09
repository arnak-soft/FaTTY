#pragma once

#include <string>
#include <string_view>

namespace fatty {

// Пользователю при первом запуске новой версии. 1–4 коротких пункта
// с последнего git-тега. Агент обновляет после каждого заметного изменения.
inline constexpr std::string_view kWhatsNew =
    "• Галочки, списки и меню в цветах окна, кнопка в фокусе обведена\n"
    "• Клик по заголовку сортирует список команд и не меняет сохранённый порядок\n"
    "• Импорт спрашивает «Добавить» или «Заменить», пароли в экспорт сами не попадают\n"
    "• В правке команды текст сверху, папка и shell — в блоке «Запуск»";

std::string release_version_id(std::string_view version);
bool should_show_whats_new(std::string_view last_seen, std::string_view current,
                           std::string_view notes = kWhatsNew);

}  // namespace fatty
