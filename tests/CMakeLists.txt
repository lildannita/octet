# Опция для включения покрытия
option(OCTET_COVERAGE "Enable test coverage analysis" OFF)

# Поиск необходимых библиотек
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

# Проверка, что обе библиотеки собраны
if(NOT (TARGET octet_static AND TARGET octet_shared))
    message(FATAL_ERROR "Both octet_static and octet_shared must be built to enable testing.")
endif()

# Указание источников
set(OCTET_TEST_SOURCES
    test_file_lock_guard.cpp
    test_file_utils.cpp
    test_journal_manager.cpp
    test_uuid_generator.cpp
    testing_utils.hpp
    testing_utils.cpp
    # TODO
)

# Директория сборки для исполняемых тестовых файлов
set(OCTET_TESTS_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/bin)
# Директория с заголовочными файлами библиотеки
set(OCTET_TESTS_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
# Зависимости для тестов
set(OCTET_TEST_DEPENDENCIES
    GTest::GTest
    GTest::Main
    Threads::Threads
)

# Создание исполняемого файла для тестирования со *статической* библиотекой
add_executable(octet_unit_tests_static ${OCTET_TEST_SOURCES})
set_target_properties(octet_unit_tests_static PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OCTET_TESTS_OUTPUT_DIR})
target_include_directories(octet_unit_tests_static PRIVATE ${OCTET_TESTS_INCLUDE_DIR})
target_link_libraries(octet_unit_tests_static PRIVATE octet_static
                                                      octet_platform
                                                      ${OCTET_TEST_DEPENDENCIES})

# Создание исполняемого файла для тестирования со *динамической* библиотекой
add_executable(octet_unit_tests_shared ${OCTET_TEST_SOURCES})
set_target_properties(octet_unit_tests_shared PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OCTET_TESTS_OUTPUT_DIR})
target_include_directories(octet_unit_tests_shared PRIVATE ${OCTET_TESTS_INCLUDE_DIR})
target_link_libraries(octet_unit_tests_shared PRIVATE octet_shared
                                                      octet_platform
                                                      ${OCTET_TEST_DEPENDENCIES})

# Настройка покрытия
if(OCTET_COVERAGE)
    # Проверка наличия необходимых инструментов
    find_program(GCOV_PATH gcov)
    find_program(GCOVR_PATH gcovr)

    if(NOT GCOV_PATH)
        message(FATAL_ERROR "gcov not found! Cannot enable coverage.")
    endif()

    if(NOT GCOVR_PATH)
        message(FATAL_ERROR "gcovr not found! Cannot enable coverage.")
    endif()

    if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
        message(FATAL_ERROR "Code coverage is not supported for ${CMAKE_CXX_COMPILER_ID} compiler.")
    endif()

    message(STATUS "Coverage enabled for tests with ${CMAKE_CXX_COMPILER_ID} compiler.")

    # Флаги для измерения покрытия
    set(COVERAGE_COMPILER_FLAGS -g
                                -O0
                                --coverage
                                -fprofile-arcs
                                -ftest-coverage)

    # Добавляем флаги к библиотекам
    foreach(target IN ITEMS octet_static octet_shared)
        target_compile_options(${target} PRIVATE ${COVERAGE_COMPILER_FLAGS})
        target_link_options(${target} PRIVATE "--coverage")
    endforeach()

    # Добавляем флаги к исполняемым файлам тестов
    foreach(target IN ITEMS octet_unit_tests_static octet_unit_tests_shared)
        target_compile_options(${target} PRIVATE ${COVERAGE_COMPILER_FLAGS})
        target_link_options(${target} PRIVATE "--coverage")
    endforeach()

    # Директория для хранения отчётов
    set(COVERAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/coverage)

    # Общие параметры gcovr
    set(GCOVR_COMMON_FLAGS
        --root=${CMAKE_SOURCE_DIR}
        --object-directory=${CMAKE_BINARY_DIR}
        --exclude='/usr/.*'
        --exclude='/opt/.*'
        --exclude='.*gtest.*'
        --exclude='.*tests/.*'
        --exclude='.*include/c\\+\\+/.*'
        --exclude='.*bits/.*'
        --exclude-throw-branches
        --exclude-unreachable-branches
    )

    # Общая функция для создания цели покрытия
    function(setup_octet_coverage_target target_name executable)
        # Директория для хранения отчётов по текущей версии библиотеки
        set(coverage_report_dir "${COVERAGE_DIR}/${target_name}_report")

        add_custom_target(${target_name}
            # Очистка предыдущих файлов .gcda для точного измерения
            COMMAND find ${CMAKE_BINARY_DIR} -name "*.gcda" -delete || true

            # Запуск тестов
            COMMAND ${OCTET_TESTS_OUTPUT_DIR}/${executable}

            # Создание директории для отчета
            COMMAND ${CMAKE_COMMAND} -E make_directory ${coverage_report_dir}

            # Генерация HTML-отчета
            COMMAND ${GCOVR_PATH} ${GCOVR_COMMON_FLAGS}
                                  --html --html-details
                                  --output=${coverage_report_dir}/index.html
  
            # Генерация XML отчета для интеграции с инструментами CI
            COMMAND ${GCOVR_PATH} ${GCOVR_COMMON_FLAGS}
                                  --xml
                                  --output=${coverage_report_dir}/coverage.xml
            
            # Вывод краткого текстового отчета в консоль
            COMMAND ${GCOVR_PATH} ${GCOVR_COMMON_FLAGS}
                                  --txt

            # Команды запускаем из основной директории сборки
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating coverage report for ${target_name} at ${coverage_report_dir}"
        )
    endfunction()

    # Создаем отдельные цели для разных типов библиотек
    setup_octet_coverage_target(octet_coverage_static octet_unit_tests_static)
    setup_octet_coverage_target(octet_coverage_shared octet_unit_tests_shared)

    # Создаем общую цель, которая запускает оба типа тестов
    add_custom_target(octet_coverage
        DEPENDS octet_coverage_static octet_coverage_shared
        COMMENT "Generated both static and shared library coverage reports"
    )
endif()

# Подключаем CTest
enable_testing()
add_test(NAME octet_unit_tests_static COMMAND ${OCTET_TESTS_OUTPUT_DIR}/octet_unit_tests_static)
add_test(NAME octet_unit_tests_shared COMMAND ${OCTET_TESTS_OUTPUT_DIR}/octet_unit_tests_shared)
