#pragma once

#include <string>
#include <string_view>

namespace fatty {

// Пользователю при первом запуске новой версии. 1–4 коротких пункта
// с последнего git-тега. Агент обновляет после каждого заметного изменения.
inline constexpr std::string_view kWhatsNew =
    "• Shell: история листается колёсиком и полосой справа\n"
    "• Выделение в Shell сразу копируется в буфер\n"
    "• Вывод команд в Shell включён при запуске, папка совпадает с командой\n"
    "• После команды ввод продолжается сразу за приглашением";

std::string release_version_id(std::string_view version);
bool should_show_whats_new(std::string_view last_seen, std::string_view current,
                           std::string_view notes = kWhatsNew);

}  // namespace fatty
