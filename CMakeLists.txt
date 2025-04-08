# Указание минимальной версии CMake
cmake_minimum_required(VERSION 3.17 FATAL_ERROR)

# Инициализация проекта
project(octet LANGUAGES CXX)

# Указание версии проекта
set(octet_VERSION_MAJOR 0)
set(octet_VERSION_MINOR 1)
set(octet_VERSION_PATCH 0)

# Настройка стандарта C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Поиск необходимых библиотек
find_package(Boost REQUIRED COMPONENTS system)

# Опции сборки
option(OCTET_BUILD_SHARED_LIB "Build shared version of the library" ON)
option(OCTET_BUILD_STATIC_LIB "Build static version of the library" OFF)
option(OCTET_BUILD_APP "Build CLI executable" ON)
option(OCTET_BUILD_TESTS "Build tests" OFF)

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

# Указание источников
set(OCTET_SOURCES
    src/storage/uuid_generator.cpp
    src/utils/file_lock_guard.cpp
    src/utils/file_utils.cpp
    src/utils/logger.cpp
    # TODO
)

# Для подключения заголовков
set(OCTET_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Наименование для всех библиотек и для исполняемого файла
set(OCTET_BASENAME "octet")

# Функция для создания библиотеки (динамическая/статическая)
function(add_octet_library type libname)
add_library(${libname} ${type} ${OCTET_SOURCES})
    set_target_properties(${libname} PROPERTIES OUTPUT_NAME ${OCTET_BASENAME}
                                                # Настройки для, в основном, Debug-сборки
                                                LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib
                                                ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
    target_link_libraries(${libname} PRIVATE octet_platform
                                             Boost::system)
    target_include_directories(${libname} PRIVATE ${OCTET_INCLUDE_DIR})
endfunction(add_octet_library)

# Создание динамической библиотеки
if(OCTET_BUILD_SHARED_LIB)
    add_octet_library(SHARED octet_shared)
endif()

# Создание статической библиотеки
if(OCTET_BUILD_STATIC_LIB)
    add_octet_library(STATIC octet_static)
endif()

# Создаение исполняемого файла
if(OCTET_BUILD_APP)
    add_executable(octet app/main.cpp)
    set_target_properties(octet PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
    target_include_directories(octet PRIVATE ${OCTET_INCLUDE_DIR})

    # Динамическая библиотека в приоритете
    if(OCTET_BUILD_SHARED_LIB)
        target_link_libraries(octet PRIVATE octet_shared)
    elseif(OCTET_BUILD_STATIC_LIB)
        target_link_libraries(octet PRIVATE octet_static)
    else()
        message(FATAL_ERROR "Internal error! Can't build application without library.")
    endif()
endif()

# Добавление тестов
if(OCTET_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Настройка установки
include(CMakePackageConfigHelpers)

function(octet_install_library target)
    install(TARGETS ${target} EXPORT octet_targets
                              LIBRARY DESTINATION lib
                              ARCHIVE DESTINATION lib
                              RUNTIME DESTINATION bin
                              INCLUDES DESTINATION include)
endfunction()

if(OCTET_BUILD_SHARED_LIB)
    octet_install_library(octet_shared)
endif()

if(OCTET_BUILD_STATIC_LIB)
    octet_install_library(octet_static)
endif()

# Добавляем платформенную библиотеку в экспортируемый набор
install(TARGETS octet_platform EXPORT octet_targets)

# Копируем все заголовочные файлы в директорию сборки
install(DIRECTORY include/ DESTINATION include)

# Установка экспортируемых таргетов
install(EXPORT octet_targets
    FILE octetTargets.cmake
    NAMESPACE octet::
    DESTINATION lib/cmake/octet
)
set(OCTET_CONFIGURATION_DIR lib/cmake/octet)

# Генерация версии и конфигурационного файла
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/octetConfigVersion.cmake"
    VERSION "${octet_VERSION_MAJOR}.${octet_VERSION_MINOR}.${octet_VERSION_PATCH}"
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/octetConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/octetConfig.cmake"
    INSTALL_DESTINATION ${OCTET_CONFIGURATION_DIR}
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/octetConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/octetConfigVersion.cmake"
    DESTINATION ${OCTET_CONFIGURATION_DIR}
)
