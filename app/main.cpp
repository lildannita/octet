#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <map>
#include <algorithm>
#include <optional>

#include "storage/storage_manager.hpp"
#include "logger.hpp"

// Вывод справки
void printHelp(const char *executable)
{
    std::cout
        << "Использование:\n"
        << "  " << executable << " --storage=ПУТЬ [ОПЦИИ] [РЕЖИМ РАБОТЫ] [АРГУМЕНТЫ]\n\n"

        << "Описание:\n"
        << "  Хранилище JSON-совместимых UTF-8 строк с журналированием, снапшотами и "
           "UUID-идентификаторами.\n\n"

        << "Путь к хранилищу (--storage):\n"
        << "  Параметр --storage указывает директорию, где будут храниться данные,\n"
        << "  а именно журналы и снапшоты. Эта директория должна быть доступной для записи.\n"
        << "  Пример: --storage=~/octet/mystorage\n\n"

        << "Общие опции:\n"
        << "  --snapshot-operations=ЧИСЛО   Порог операций до снапшота (по умолчанию: 100)\n"
        << "  --snapshot-minutes=ЧИСЛО      Интервал снапшотов в минутах (по умолчанию: 10)\n"
        << "  --log-level=УРОВЕНЬ           Установить уровень логирования (по умолчанию: info)\n"
        << "  --log-disable-colors          Запретить цветной вывод (по умолчанию: включен)\n"
        << "  --help                        Показать справку\n\n"

        << "Режимы работы:\n"
        << "  По умолчанию octet выполняет однократную команду, если не указаны "
           "--interactive или --server.\n\n"

        << "=== Однократное выполнение команды ===\n"
        << "  Запуск: octet --storage=ПУТЬ <КОМАНДА> [АРГУМЕНТЫ]\n"
        << "  Команда и аргументы указываются в командной строке.\n"
        << "  Доступные команды:\n"
        << "    insert <СТРОКА>              Вставить строку и получить ее UUID\n"
        << "    get <UUID>                   Получить строку по UUID\n"
        << "    update <UUID> <СТРОКА>       Обновить строку по UUID\n"
        << "    delete <UUID>                Удалить строку по UUID\n"
        << "    snapshot                     Принудительно создать снапшот\n\n"

        << "=== Интерактивный режим ===\n"
        << "  Запуск: octet --storage=ПУТЬ --interactive\n"
        << "  В интерактивном режиме команды вводятся построчно.\n"
        << "  Доступные команды:\n"
        << "    insert <СТРОКА>              Вставить строку и получить ее UUID\n"
        << "    get <UUID>                   Получить строку по UUID\n"
        << "    update <UUID> <СТРОКА>       Обновить строку по UUID\n"
        << "    delete <UUID>                Удалить строку по UUID\n"
        << "    snapshot                     Принудительно создать снапшот\n"
        << "    set-snapshot-operations <N>  Изменить порог операций для снапшота\n"
        << "    set-snapshot-minutes <N>     Изменить интервал снапшота в минутах\n"
        << "    set-log-level <УРОВЕНЬ>      Установить уровень логирования\n\n"

        << "=== Серверный режим ===\n"
        << "  Запуск: octet --storage=ПУТЬ --server [ОПЦИИ]\n"
        << "  Опции:\n"
        << "    --socket=ПУТЬ                Путь к Unix-сокету (вместо TCP)\n"
        << "    --port=ПОРТ                  Порт HTTP-сервера (по умолчанию: 8080)\n"
        << "    --address=АДРЕС              Адрес привязки (по умолчанию: 127.0.0.1)\n\n"

        << "Уровни логирования:\n"
        << "  trace (0)                     Детальная трассировка для отладки\n"
        << "  debug (1)                     Отладочные сообщения\n"
        << "  info (2)                      Информационные сообщения\n"
        << "  warning (3)                   Предупреждения, не являющиеся ошибками\n"
        << "  error (4)                     Ошибки, не прерывающие работу программы\n"
        << "  critical (5)                  Критические ошибки, прерывающие выполнение\n";
}

// Преобразование строки в LogLevel
std::optional<octet::LogLevel> parseLogLevel(const std::string &level)
{
    std::string lowered = level;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lowered == "trace" || lowered == "0")
        return octet::LogLevel::TRACE;
    if (lowered == "debug" || lowered == "1")
        return octet::LogLevel::DEBUG;
    if (lowered == "info" || lowered == "2")
        return octet::LogLevel::INFO;
    if (lowered == "warning" || lowered == "3")
        return octet::LogLevel::WARNING;
    if (lowered == "error" || lowered == "4")
        return octet::LogLevel::ERROR;
    if (lowered == "critical" || lowered == "5")
        return octet::LogLevel::CRITICAL;
    return std::nullopt;
}

// Получение значения опции из аргументов командной строки
std::optional<std::string> getOptionValue(const std::string &option, std::vector<std::string> &args)
{
    for (size_t i = 0; i < args.size(); ++i) {
        const auto &arg = args[i];
        // Проверка формата `--option=value`
        size_t pos = arg.find('=');
        if (pos != std::string::npos && arg.substr(0, pos) == option) {
            const std::string value = arg.substr(pos + 1);
            // Удаляем строку из вектора
            args.erase(args.begin() + i);
            return value;
        }
        // Проверка формата `--option value`
        if (arg == option && i + 1 < args.size()) {
            const std::string value = args[i + 1];
            // Удаляем строку с параметром и строку со значением из вектора
            args.erase(args.begin() + i, args.begin() + i + 2);
            return value;
        }
    }
    return std::nullopt;
}

// Проверка наличия флага в аргументах командной строки
bool hasFlag(const std::string &flag, std::vector<std::string> &args)
{
    auto it = std::find(args.begin(), args.end(), flag);
    if (it != args.end()) {
        // Удаляем найденный флаг из вектора
        args.erase(it);
        return true;
    }
    return false;
}

// Проверка оставшихся флагов: если флаги остались, то это ошибка
bool checkLastArgs(const std::vector<std::string> &args)
{
    if (args.empty()) {
        return true;
    }

    LOG_ERROR << "Ошибка: неизвестные аргументы:";
    for (const auto &arg : args) {
        LOG_ERROR << "\t" << arg;
    }
    return false;
}

int main(int argc, char *argv[])
{
    // Получение команды запуска
    auto executable = (argc > 0) ? argv[0] : "";
    // Преобразование аргументов в вектор строк для удобства работы
    std::vector<std::string> args(argv + 1, argv + argc);

    // Проверка на --help
    if (args.empty() || hasFlag("--help", args)) {
        printHelp(executable);
        return 0;
    }

    // Инициализация логгера в режиме по умолчанию
    octet::Logger::getInstance().enable();

    // Получение пути к хранилищу
    const auto storageOption = getOptionValue("--storage", args);
    if (!storageOption) {
        LOG_ERROR << "Ошибка: не указан путь к хранилищу (--storage=ПУТЬ)\n";
        printHelp(executable);
        return 1;
    }
    const auto storagePath = std::filesystem::path(*storageOption);

    // Получение параметров
    const auto interactiveMode = hasFlag("--interactive", args);
    const auto serverMode = hasFlag("--server", args);
    const auto disableColorLog = hasFlag("--log-disable-colors", args);
    std::optional<size_t> snapshotOpsThreshold;
    std::optional<size_t> snapshotTimeThreshold;
    std::optional<octet::LogLevel> logLevel;

    // Парсинг порога операций для снапшота
    const auto opsOption = getOptionValue("--snapshot-operations", args);
    if (opsOption.has_value()) {
        try {
            snapshotOpsThreshold = std::stoul(*opsOption);
        }
        catch (const std::exception &e) {
            LOG_ERROR << "Ошибка: некорректное значение для --snapshot-operations";
            return 1;
        }
    }

    // Парсинг интервала снапшота в минутах
    const auto minutesOption = getOptionValue("--snapshot-minutes", args);
    if (minutesOption.has_value()) {
        try {
            snapshotTimeThreshold = std::stoul(*minutesOption);
        }
        catch (const std::exception &e) {
            LOG_ERROR << "Ошибка: некорректное значение для --snapshot-minutes";
            return 1;
        }
    }

    // Парсинг уровня логирования
    const auto levelOption = getOptionValue("--log-level", args);
    if (levelOption.has_value()) {
        const auto rawLogLevel = parseLogLevel(*levelOption);
        if (rawLogLevel.has_value()) {
            logLevel = parseLogLevel(*levelOption);
        }
        else {
            LOG_ERROR << "Ошибка: некорректное значение для --log-level";
            return 1;
        }
    }

    // TODO: необходимо распарсить дополнительные опции для серверного режима

    // Для интерактивного и серверного режимов не должно остаться аргументов
    if ((interactiveMode || serverMode) && !checkLastArgs(args)) {
        return 1;
    }

    // Инициализация StorageManager
    octet::StorageManager storage(std::move(storagePath));
    // Настраиваем логгер согласно заданным параметрам
    if (logLevel.has_value()) {
        octet::Logger::getInstance().setMinLogLevel(*logLevel);
    }
    octet::Logger::getInstance().setUseColors(!disableColorLog);

    if (serverMode) {
        // TODO: запуск интерактивного режима
        return 0;
    }

    if (interactiveMode) {
        // TODO: запуск интерактивного режима
        return 0;
    }

    // TODO: выполнение команды
    return 0;
}