#include "cli/commands.hpp"

#include <algorithm>

namespace octet::cli {
CommandProcessor::CommandProcessor(octet::StorageManager &storage)
    : storage_(storage)
{
    // Регистрация всех доступных команд

    // Команда вставки строки
    commands_["insert"]
        = { 1, false,
            [this](const std::vector<std::string> &args, std::ostream &out) -> CommandResult {
                const auto result = storage_.insert(args.at(0));
                if (result.has_value()) {
                    out << *result << "\n";
                    return CommandResult::SUCCESS;
                }
                return CommandResult::FAILURE;
            } };

    // Команда получения строки
    commands_["get"]
        = { 1, false,
            [this](const std::vector<std::string> &args, std::ostream &out) -> CommandResult {
                const auto result = storage_.get(args.at(0));
                if (result.has_value()) {
                    out << *result << "\n";
                    return CommandResult::SUCCESS;
                }
                return CommandResult::FAILURE;
            } };

    // Команда обновления строки
    commands_["update"]
        = { 2, false,
            [this](const std::vector<std::string> &args, std::ostream &) -> CommandResult {
                const auto result = storage_.update(args.at(0), args.at(1));
                return result ? CommandResult::SUCCESS : CommandResult::FAILURE;
            } };

    // Команда удаления строки
    commands_["remove"]
        = { 1, false,
            [this](const std::vector<std::string> &args, std::ostream &) -> CommandResult {
                const auto result = storage_.remove(args.at(0));
                return result ? CommandResult::SUCCESS : CommandResult::FAILURE;
            } };

    // Команда создания снапшота
    commands_["snapshot"]
        = { 0, true, [this](const std::vector<std::string> &, std::ostream &) -> CommandResult {
               const auto result = storage_.createSnapshot();
               return result ? CommandResult::SUCCESS : CommandResult::FAILURE;
           } };

    // Команда установки порога операций для снапшота
    commands_["set-snapshot-operations"]
        = { 1, true, [this](const std::vector<std::string> &args, std::ostream &) -> CommandResult {
               try {
                   const auto threshold = std::stoul(args.at(0));
                   storage_.setSnapshotOperationsThreshold(threshold);
                   return CommandResult::SUCCESS;
               }
               catch (const std::exception &e) {
                   LOG_ERROR << "Ошибка: некорректное значение для порога операций";
                   return CommandResult::FAILURE;
               }
           } };

    // Команда установки интервала снапшота в минутах
    commands_["set-snapshot-minutes"]
        = { 1, true, [this](const std::vector<std::string> &args, std::ostream &) -> CommandResult {
               try {
                   const auto minutes = std::stoul(args.at(0));
                   storage_.setSnapshotTimeThreshold(minutes);
                   return CommandResult::SUCCESS;
               }
               catch (const std::exception &e) {
                   LOG_ERROR << "Ошибка: некорректное значение для интервала снапшотов\n";
                   return CommandResult::FAILURE;
               }
           } };

    // Команда выхода
    commands_["exit"]
        = { 0, true, [this](const std::vector<std::string> &, std::ostream &) -> CommandResult {
               return CommandResult::EXIT;
           } };

    // Команда вывода справки
    commands_["help"]
        = { 0, true, [this](const std::vector<std::string> &, std::ostream &out) -> CommandResult {
               out << "Доступные команды:\n"
                   << "  insert \"<СТРОКА>\"          Вставить строку и получить ее UUID\n"
                   << "  get <UUID>                   Получить строку по UUID\n"
                   << "  update <UUID> \"<СТРОКА>\"   Обновить строку по UUID\n"
                   << "  remove <UUID>                Удалить строку по UUID\n"
                   << "  snapshot                     Принудительно создать снапшот\n"
                   << "  set-snapshot-operations <N>  Изменить порог операций для снапшота\n"
                   << "  set-snapshot-minutes <N>     Изменить интервал снапшота в минутах\n"
                   << "  exit                         Выход из интерактивного режима\n"
                   << "  help                         Показать справку по доступным командам\n\n";
               return CommandResult::SUCCESS;
           } };
}

CommandResult CommandProcessor::do_execute(std::vector<std::string> &args, bool singleShotMode,
                                           std::ostream &out) const
{
    if (args.empty()) {
        LOG_ERROR << "Ошибка: необходимо указать команду для выполнения.\n"
                  << "Введите `help` для получения списка доступных команд";
        return CommandResult::FAILURE;
    }

    // Достаем команду из аргументов
    const auto commandName = std::move(args.front());
    args.erase(args.begin());

    auto it = commands_.find(commandName);
    if (it == commands_.end()) {
        LOG_ERROR << "Ошибка: неизвестная команда: " << commandName << ".\n"
                  << "Введите `help` для получения списка доступных команд";
        return CommandResult::FAILURE;
    }

    const auto &cmd = it->second;
    // Проверка количества аргументов
    if (args.size() != cmd.argsCount) {
        LOG_ERROR << "Ошибка: неправильное использование команды " << commandName << ".\n"
                  << "Введите `help` для получения информации об использовании команд";
        return CommandResult::FAILURE;
    }

    // Проверка возможности выполнения команды
    if (singleShotMode && cmd.onlyForInteractive) {
        LOG_ERROR << "Ошибка: команда " << commandName
                  << " доступна только в интерактивном режиме.\n"
                  << "Введите `help` для получения информации об использовании команд";
        return CommandResult::FAILURE;
    }

    // Выполнение команды
    return cmd.execute(args, out);
}
} // namespace octet::cli
