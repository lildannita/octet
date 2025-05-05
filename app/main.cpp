#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "cli/commands.hpp"
#include "storage/storage_manager.hpp"
#include "logger.hpp"

// Вывод справки
void printHelp(const char *executable)
{
    std::cout
        << "Использование:\n"
        << "  " << executable << " --storage=ПУТЬ [ОПЦИИ] [РЕЖИМ РАБОТЫ] [АРГУМЕНТЫ]\n\n"

        << "Описание:\n"
        << "  Хранилище UTF-8 строк с механизмом WAL, снапшотами и UUID-идентификаторами.\n\n"

        << "Путь к хранилищу (--storage):\n"
        << "  Параметр --storage указывает директорию, где будут храниться данные,\n"
        << "  а именно журналы и снапшоты. Эта директория должна быть доступной для записи.\n"
        << "  Пример: --storage=~/octet/mystorage\n\n"

        << "Общие опции:\n"
        << "  --snapshot-operations=ЧИСЛО    Порог операций до снапшота (по умолчанию: 100)\n"
        << "  --snapshot-minutes=ЧИСЛО       Интервал снапшотов в минутах (по умолчанию: 10)\n"
        << "  --disable-warnings             Отключить вывод текстовых сообщений-предупреждений\n"
        << "  --help                         Показать справку\n\n"

        << "Режимы работы:\n"
        << "  По умолчанию octet выполняет однократную команду, если не указаны "
           "--interactive или --server.\n\n"

        << "=== Однократное выполнение команды ===\n"
        << "  Запуск: octet --storage=ПУТЬ <КОМАНДА> [АРГУМЕНТЫ]\n"
        << "  Команда и аргументы указываются в командной строке.\n"
        << "  Доступные команды:\n"
        << "    insert \"<СТРОКА>\"          Вставить строку и получить ее UUID\n"
        << "    get <UUID>                   Получить строку по UUID\n"
        << "    update <UUID> \"<СТРОКА>\"   Обновить строку по UUID\n"
        << "    remove <UUID>                Удалить строку по UUID\n\n"

        << "=== Интерактивный режим ===\n"
        << "  Запуск: octet --storage=ПУТЬ --interactive\n"
        << "  В интерактивном режиме команды вводятся построчно.\n"
        << "  Доступные команды:\n"
        << "    insert \"<СТРОКА>\"          Вставить строку и получить ее UUID\n"
        << "    get <UUID>                   Получить строку по UUID\n"
        << "    update <UUID> \"<СТРОКА>\"   Обновить строку по UUID\n"
        << "    remove <UUID>                Удалить строку по UUID\n"
        << "    snapshot                     Принудительно создать снапшот\n"
        << "    set-snapshot-operations <N>  Изменить порог операций для снапшота\n"
        << "    set-snapshot-minutes <N>     Изменить интервал снапшота в минутах\n"
        << "    exit                         Выход из интерактивного режима\n"
        << "    help                         Показать справку по доступным командам\n\n"

        << "=== Серверный режим ===\n"
        << "  Запуск: octet --storage=ПУТЬ --server [ОПЦИИ]\n"
        << "  Опции:\n"
        << "    --socket=ПУТЬ                Путь к Unix-сокету (вместо TCP)\n"
        << "    --port=ПОРТ                  Порт HTTP-сервера (по умолчанию: 8080)\n"
        << "    --address=АДРЕС              Адрес привязки (по умолчанию: 127.0.0.1)\n\n";
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

    // Инициализация логгера
    octet::Logger::getInstance().enable(true, std::nullopt, octet::LogLevel::WARNING, true, false);

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
    const auto disableWarnings = hasFlag("disable-warnings", args);
    std::optional<size_t> snapshotOpsThreshold;
    std::optional<size_t> snapshotTimeThreshold;

    // Если отключены предупреждения
    if (disableWarnings) {
        octet::Logger::getInstance().setMinLogLevel(octet::LogLevel::ERROR);
    }

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

    // TODO: необходимо распарсить дополнительные опции для серверного режима

    // Для интерактивного и серверного режимов не должно остаться аргументов
    if ((interactiveMode || serverMode) && !checkLastArgs(args)) {
        return 1;
    }

    // Инициализация StorageManager
    octet::StorageManager storage(std::move(storagePath));

    if (serverMode) {
        // TODO: запуск интерактивного режима
        return 0;
    }

    if (interactiveMode) {
        // TODO: запуск интерактивного режима
        return 0;
    }

    const auto result = octet::cli::CommandProcessor::executeShot(storage, args);
    return result == octet::cli::CommandResult::SUCCESS ? 0 : 1;
}
