cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED VCPKG_TARGET_TRIPLET)
  if(MSVC)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
  elseif(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(VCPKG_TARGET_TRIPLET "x64-mingw-static" CACHE STRING "")
    set(VCPKG_HOST_TRIPLET "x64-mingw-static" CACHE STRING "")
  endif()
endif()

project(FaTTY VERSION 0.0.0 LANGUAGES CXX)

if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include("${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake")
fatty_resolve_git_version(FATTY_VERSION FATTY_VERSION_TUPLE)
message(STATUS "FaTTY version: ${FATTY_VERSION}")

configure_file(
  "${CMAKE_SOURCE_DIR}/src/app/version_generated.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/version_generated.hpp"
  @ONLY
)
file(WRITE "${CMAKE_BINARY_DIR}/generated/_version.txt" "${FATTY_VERSION}\n")
string(REPLACE "," "." FATTY_VERSION_INFO "${FATTY_VERSION_TUPLE}")
file(WRITE "${CMAKE_BINARY_DIR}/generated/_version_info.txt" "${FATTY_VERSION_INFO}\n")

find_package(wxWidgets CONFIG REQUIRED)
find_package(Libssh2 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(FATTY_CORE_SOURCES
  src/core/util.cpp
  src/core/paths.cpp
  src/core/vault.cpp
  src/core/dpapi.cpp
  src/core/store.cpp
  src/core/lockout.cpp
  src/core/journal.cpp
  src/core/config_io.cpp
  src/core/backup.cpp
  src/core/presets.cpp
  src/core/quote.cpp
  src/core/placeholders.cpp
  src/app/version.cpp
)

set(FATTY_NET_SOURCES
  src/net/ssh_session.cpp
  src/net/sftp_session.cpp
  src/net/updates.cpp
  src/putty/ppk.cpp
  src/putty/putty.cpp
)

set(FATTY_UI_SOURCES
  src/ui/theme.cpp
  src/ui/striped_list.cpp
  src/ui/chrome.cpp
  src/ui/layout.cpp
  src/ui/widgets.cpp
  src/ui/dialogs.cpp
  src/ui/settings_dialog.cpp
  src/ui/files_window.cpp
  src/ui/journal_window.cpp
  src/ui/bundle_steps_window.cpp
  src/ui/help_window.cpp
  src/ui/splash.cpp
  src/ui/app_frame.cpp
  src/app/single_instance.cpp
  src/app/main.cpp
)

add_library(fatty_core STATIC ${FATTY_CORE_SOURCES})
target_include_directories(fatty_core PUBLIC
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(fatty_core PUBLIC
  OpenSSL::Crypto
  nlohmann_json::nlohmann_json
)
if(WIN32)
  target_link_libraries(fatty_core PUBLIC crypt32 rpcrt4 shell32 ole32)
endif()

add_library(fatty_net STATIC ${FATTY_NET_SOURCES})
target_include_directories(fatty_net PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(fatty_net PUBLIC fatty_core Libssh2::libssh2 OpenSSL::SSL OpenSSL::Crypto)
if(WIN32)
  target_link_libraries(fatty_net PUBLIC ws2_32 bcrypt winhttp)
endif()

add_executable(fatty WIN32 ${FATTY_UI_SOURCES} src/app/fatty.rc)
target_include_directories(fatty PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")
# Неявное wxString(const char*) идёт через wxConvLibc, т.е. через ANSI-кодовую
# страницу (CP1251 на русской Windows), и UTF-8-литералы превращаются в
# крякозябры. Флаг убирает эту перегрузку: текст UI пишем как L"…", а UTF-8
# std::string из core/net переводим явным wxString::FromUTF8().
target_compile_definitions(fatty PRIVATE wxNO_IMPLICIT_WXSTRING_ENCODING)
target_link_libraries(fatty PRIVATE
  fatty_core
  fatty_net
  wx::core
  wx::base
)
if(WIN32)
  target_link_libraries(fatty PRIVATE dwmapi uxtheme user32 gdi32 comctl32 ole32 uuid advapi32)
endif()

if(MSVC)
  target_compile_options(fatty_core PUBLIC /utf-8 /W3 /permissive-)
  target_compile_definitions(fatty_core PUBLIC
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    FATTY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  set_property(TARGET fatty fatty_core fatty_net PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  # Манифест уже приходит из fatty.rc (wx/msw/wx.rc: comctl32 v6 + DPI).
  # По умолчанию link.exe встраивает ещё и свой (/MANIFEST:EMBED,ID=1), и cvtres
  # падает с CVT1100: duplicate resource type:MANIFEST name:1. Отключаем линкерный.
  target_link_options(fatty PRIVATE "/MANIFEST:NO")
else()
  target_compile_options(fatty_core PUBLIC -Wall -Wextra -Wno-unused-parameter)
  target_compile_definitions(fatty_core PUBLIC
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    FATTY_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  if(MINGW)
    target_link_options(fatty PRIVATE -static -static-libgcc -static-libstdc++)
    # MinGW GCC линкует свой default-manifest.o (только longPathAware), и он
    # конфликтует с нашим манифестом из fatty.rc (comctl32 v6 для тёмного режима):
    # ld падает с «multiple non-default manifests». Подсовываем через -B пустой
    # default-manifest.o, чтобы единственным манифестом остался наш.
    set(_shim_dir "${CMAKE_BINARY_DIR}/mingw-manifest-shim")
    set(_shim_obj "${_shim_dir}/default-manifest.o")
    if(NOT EXISTS "${_shim_obj}")
      file(MAKE_DIRECTORY "${_shim_dir}")
      file(WRITE "${_shim_dir}/empty.cpp" "")
      execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -c "${_shim_dir}/empty.cpp" -o "${_shim_obj}"
                      RESULT_VARIABLE _shim_rc)
      if(NOT _shim_rc EQUAL 0)
        message(WARNING "Не удалось собрать manifest-shim; тёмная тема на MinGW может не включиться")
      endif()
    endif()
    if(EXISTS "${_shim_obj}")
      target_link_options(fatty PRIVATE "-B${_shim_dir}")
    endif()
  endif()
endif()

set_target_properties(fatty PROPERTIES OUTPUT_NAME "FaTTY")

enable_testing()
add_executable(fatty_tests
  tests/test_main.cpp
  tests/test_vault.cpp
  tests/test_store.cpp
  tests/test_quote.cpp
  tests/test_journal.cpp
  tests/test_backup.cpp
  tests/test_placeholders.cpp
  tests/test_updates.cpp
)
target_include_directories(fatty_tests PRIVATE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(fatty_tests PRIVATE fatty_core fatty_net)
if(MSVC)
  set_property(TARGET fatty_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
add_test(NAME fatty_tests COMMAND fatty_tests)

install(TARGETS fatty RUNTIME DESTINATION .)
install(FILES "${CMAKE_BINARY_DIR}/generated/_version.txt" DESTINATION .)
install(FILES "${CMAKE_SOURCE_DIR}/assets/app.ico" DESTINATION . OPTIONAL)
