# Указание минимальной версии CMake
cmake_minimum_required(VERSION 3.17 FATAL_ERROR)

# Инициализация проекта
project(octet
    VERSION 0.2.0
    DESCRIPTION "Octet library for UTF-8 string storage"
    LANGUAGES CXX
)

# Настройка стандарта C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Подключаем модуль с переменными для стандартных путей установки
include(GNUInstallDirs)


# Поиск необходимых библиотек
find_package(Threads REQUIRED)
find_package(Boost REQUIRED CONFIG COMPONENTS system)

# Опции сборки
option(OCTET_BUILD_SHARED_LIB "Build shared version of the library" ON)
option(OCTET_BUILD_STATIC_LIB "Build static version of the library" OFF)
option(OCTET_BUILD_APP "Build CLI executable" OFF)
option(OCTET_BUILD_TESTS "Build tests" OFF)

# TODO: При разработке CLI расскоментировать
# option(OCTET_BUILD_APP "Build CLI executable" ON)

# Определение платформы
add_library(octet_platform INTERFACE)
if(WIN32)
    message(STATUS "Platform: Windows")
    target_compile_definitions(octet_platform INTERFACE OCTET_PLATFORM_WINDOWS OCTET_PLATFORM_SUPPORTED)
elseif(APPLE)
    message(STATUS "Platform: macOS")
    target_compile_definitions(octet_platform INTERFACE OCTET_PLATFORM_MACOS OCTET_PLATFORM_UNIX OCTET_PLATFORM_SUPPORTED)
elseif(UNIX)
    message(STATUS "Platform: Linux")
    target_compile_definitions(octet_platform INTERFACE OCTET_PLATFORM_LINUX OCTET_PLATFORM_UNIX OCTET_PLATFORM_SUPPORTED)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Если включены тесты — обязательно собираем и статическую, и динамическую библиотеки
if(OCTET_BUILD_TESTS)
    message(STATUS "OCTET_BUILD_TESTS is ON: enabling both static and shared library builds for testing purposes.")
    set(OCTET_BUILD_STATIC_LIB ON CACHE BOOL "Force static lib for testing" FORCE)
    set(OCTET_BUILD_SHARED_LIB ON CACHE BOOL "Force shared lib for testing" FORCE)
endif()

# Хотя бы одна версия библиотеки должна быть выбрана
if(NOT (OCTET_BUILD_SHARED_LIB OR OCTET_BUILD_STATIC_LIB))
    message(FATAL_ERROR "At least one of BUILD_STATIC_LIB or BUILD_SHARED_LIB must be ON.")
endif()

# Указание публичных заголовочных файлов
set(OCTET_PUBLIC_HEADERS
    include/logger.hpp
    include/storage/journal_manager.hpp
    include/storage/storage_manager.hpp
    include/storage/uuid_generator.hpp
)

# Указание приватных заголовочных файлов
set(OCTET_PRIVATE_HEADERS
    include/utils/compiler.hpp
    include/utils/file_lock_guard.hpp
    include/utils/file_utils.hpp
)

# Указание исходников
set(OCTET_SOURCES
    src/logger.cpp
    src/storage/journal_manager.cpp
    src/storage/storage_manager.cpp
    src/storage/uuid_generator.cpp
    src/utils/file_lock_guard.cpp
    src/utils/file_utils.cpp
)

# Для подключения заголовков
set(OCTET_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Наименование для всех библиотек и для исполняемого файла
set(OCTET_BASENAME "octet")
# Директория для установки заголовков
set(OCTET_INSTALL_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}/${OCTET_BASENAME}")

# Создание динамической библиотеки
if(OCTET_BUILD_SHARED_LIB)
    add_library(octet_shared SHARED
        ${OCTET_SOURCES}
        ${OCTET_PUBLIC_HEADERS}
        ${OCTET_PRIVATE_HEADERS}
    )
    add_library(octet::shared ALIAS octet_shared)
    set_target_properties(octet_shared PROPERTIES
        OUTPUT_NAME "${OCTET_BASENAME}"
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        EXPORT_NAME "shared"
    )
    target_link_libraries(octet_shared PRIVATE octet_platform)
    target_include_directories(octet_shared
        PUBLIC
            $<BUILD_INTERFACE:${OCTET_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:${OCTET_INSTALL_INCLUDE_DIR}>
    )
endif()

# Создание статической библиотеки
if(OCTET_BUILD_STATIC_LIB)
    add_library(octet_static STATIC
        ${OCTET_SOURCES}
        ${OCTET_PUBLIC_HEADERS}
        ${OCTET_PRIVATE_HEADERS}
    )
    add_library(octet::static ALIAS octet_static)
    set_target_properties(octet_static PROPERTIES
        OUTPUT_NAME "${OCTET_BASENAME}_static"
        VERSION ${PROJECT_VERSION}
        EXPORT_NAME "static"
    )
    target_link_libraries(octet_static PRIVATE octet_platform)
    target_include_directories(octet_static
        PUBLIC
            $<BUILD_INTERFACE:${OCTET_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:${OCTET_INSTALL_INCLUDE_DIR}>
    )
endif()

# Создаение исполняемого файла
if(OCTET_BUILD_APP)
    # Для подключения заголовков
    set(OCTET_INCLUDE_CLI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/app/cli")

    # Указание исходников для CLI-бинарника
    set(OCTET_CLI_SOURCES
        app/cli/main.cpp
        app/cli/interactive/commands.hpp
        app/cli/interactive/commands.cpp
        app/cli/server/connection.hpp
        app/cli/server/connection.cpp
        app/cli/server/protocol.hpp
        app/cli/server/protocol.cpp
        app/cli/server/server.hpp
        app/cli/server/server.cpp
        app/cli/3rdparty/json.hpp
    )

    add_executable(octet ${OCTET_CLI_SOURCES})
    set_target_properties(octet PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    )
    target_include_directories(octet
        PRIVATE
            ${OCTET_INCLUDE_DIR}
            ${OCTET_INCLUDE_CLI_DIR}
    )

    # Динамическая библиотека в приоритете
    if(OCTET_BUILD_SHARED_LIB)
        target_link_libraries(octet PRIVATE octet_shared Boost::system)
    elseif(OCTET_BUILD_STATIC_LIB)
        target_link_libraries(octet PRIVATE octet_static Boost::system)
    else()
        message(FATAL_ERROR "Internal error! Can't build application without library.")
    endif()

    # Установка исполняемого файла в систему
    install(TARGETS octet
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT applications
    )
endif()

# Добавление тестов
if(OCTET_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Генерируем скрипт удаления библиотеки с системы
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/octet-uninstall.cmake.in"
    "${CMAKE_BINARY_DIR}/octet-uninstall.cmake"
    @ONLY
)

# Настройка установки
include(CMakePackageConfigHelpers)

# Устанавливаем главные API заголовки
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/api/octet.hpp"
    DESTINATION "${OCTET_INSTALL_INCLUDE_DIR}"
    RENAME "${OCTET_BASENAME}"
)
# Устанавливаем публичные заголовки
foreach(_octet_header IN LISTS OCTET_PUBLIC_HEADERS)
    get_filename_component(_octet_header_we "${_octet_header}" NAME_WE)
    # Генерируем небольшой wrapper
    set(_octet_wrapper "${CMAKE_CURRENT_BINARY_DIR}/generated/${_octet_header_we}")
    file(WRITE "${_octet_wrapper}"
        "#pragma once\n#include \"${_octet_header_we}.hpp\"\n")
    # Устанавливаем реальные заголовочные файлы
    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/${_octet_header}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/octet"
    )
    # Устанавливаем wrapper как заголовок без расширения
    install(
        FILES "${_octet_wrapper}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/octet"
        RENAME "${_octet_header_we}"
    )
endforeach()

# Устанавливаем библиотеки
function(octet_install_library TARGET_NAME)
    install(TARGETS ${TARGET_NAME}
        EXPORT octet-targets
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
endfunction()
if(OCTET_BUILD_SHARED_LIB)
    octet_install_library(octet_shared)
endif()
if(OCTET_BUILD_STATIC_LIB)
    octet_install_library(octet_static)
endif()

# Добавляем платформенную библиотеку в экспортируемый набор
install(TARGETS octet_platform EXPORT octet-targets)

# Путь к директории с конфигурацией
set(OCTET_CONFIGURATION_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/${OCTET_BASENAME}")

# Экспорт целей
install(EXPORT octet-targets
    FILE "octet-targets.cmake"
    NAMESPACE octet::
    DESTINATION ${OCTET_CONFIGURATION_DIR}
)

# Генерация версии и конфигурационного файла
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/octet-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Создание конфигурационных файлов
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/octet-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/octet-config.cmake"
    INSTALL_DESTINATION ${OCTET_CONFIGURATION_DIR}
)

# Установка конфигурационных файлов
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/octet-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/octet-config-version.cmake"
    DESTINATION ${OCTET_CONFIGURATION_DIR}
)
