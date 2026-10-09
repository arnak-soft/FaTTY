#pragma once

#include <string>
#include <string_view>

namespace fatty {

// Пользователю при первом запуске новой версии. 1–4 коротких пункта
// с последнего git-тега. Агент обновляет после каждого заметного изменения.
inline constexpr std::string_view kWhatsNew =
    "• Полосы прокрутки и индикатор загрузки в тёмной теме тёмные\n"
    "• Каталог команды — куда делать cd; папка конфига и окно «Файлы» по-прежнему папка\n"
    "• Импорт спрашивает «Добавить» или «Заменить», пароли в экспорт сами не попадают\n"
    "• Удаление и выход подтверждаются своими кнопками, Enter их не нажимает";

std::string release_version_id(std::string_view version);
bool should_show_whats_new(std::string_view last_seen, std::string_view current,
                           std::string_view notes = kWhatsNew);

}  // namespace fatty
