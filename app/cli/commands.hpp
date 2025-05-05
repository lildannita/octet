#pragma once

#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "storage/storage_manager.hpp"
#include "logger.hpp"

namespace octet::cli {
/**
 * @enum CommandResult
 * @brief Тип результата выполнения команды
 */
enum class CommandResult : int {
    SUCCESS, // Успешное выполнение
    FAILURE, // Ошибка выполнения
    EXIT, // Выход из интерактивного режима
};

/**
 * @struct Command
 * @brief Структура команды
 */
struct Command {
    // Количество принимаемых аргументов
    uint8_t argsCount;
    // Команда только для интерактивного режима
    bool onlyForInteractive;
    // Функция выполнения команды
    std::function<CommandResult(const std::vector<std::string> &, std::ostream &)> execute;
};

/**
 * @class CommandProcessor
 * @brief Управляет выполнением команд
 */
class CommandProcessor {
public:
    /**
     * @brief Конструктор с указанием объекта хранилища
     * @param storage Ссылка на объект StorageManager
     * @param singleShotMode Процессор создается для однократного выполнения команды
     */
    CommandProcessor(StorageManager &storage, bool singleShotMode);

    /**
     * @brief Одноразовое выполнение команды
     * @param storage Ссылка на объект StorageManager
     * @param args Аргументы командной строки
     */
    static CommandResult executeShot(StorageManager &storage, std::vector<std::string> args);

    /**
     * @brief Запуск интерактивного режима
     * @param storage Ссылка на объект StorageManager
     * @return Код завершения
     */
    static int runInteractiveMode(StorageManager &storage);

private:
    StorageManager &storage_; // Хранилище
    std::unordered_map<std::string, Command> commands_; // Зарегестрированные команды
    bool singleShotMode_ = false;

    /**
     * @brief Фактическое выполнение команды
     * @param command Команда для выполнения
     * @param args Аргументы командной строки
     * @param out Поток вывода результата выполнения команды
     */
    CommandResult do_execute(const std::string &command, std::vector<std::string> args,
                             std::ostream &out = std::cout) const;
};
} // namespace octet::cli
