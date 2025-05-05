#include "cli/commands.hpp"

#include <algorithm>
#include <cassert>
#include "utils/compiler.hpp"

namespace {
/**
 * @brief Схлопывание строк вектора в одну указанную строку
 * @param args Ссылка на вектор строк
 * @param targetIndex Индекс строки, в которую будут добавленые следующие элементы
 */
void mergeVector(std::vector<std::string> &args, size_t targetIndex)
{
    if (args.size() <= targetIndex + 1) {
        // Нечего объединять
        return;
    }

    const size_t argsLen = args.size();
    // Итоговый размер строки: сумма длин + пробелы между ними
    size_t totalLen = argsLen - targetIndex - 1; // количество пробелов
    for (std::size_t i = targetIndex; i < argsLen; i++) {
        totalLen += args[i].size();
    }

    // Будем наращивать в указанный элемент вектора, зарезервировав нужное место
    std::string &out = args[targetIndex];
    out.reserve(totalLen);

    // Добавляем остальные строки
    for (size_t i = targetIndex + 1; i < argsLen; i++) {
        out.push_back(' ');
        out.append(std::move(args[i]));
    }

    // Удаляем все строки, идущие после targetIndex
    args.erase(args.begin() + targetIndex + 1, args.end());
}

/**
 * @brief Извлечение первого слова из строки с удалением незначащих пробелов до следующего слова
 * @param input Ссылка на исходную строку
 * @return Строка со словом (если слова нет - std::nullopt)
 */
std::optional<std::string> extractFirstWord(std::string &input)
{
    // Множество пробельных символов (аналогично std::isSpace())
    constexpr char spaces[] = " \t\n\r\f\v";

    // Поиск начала слова (пропускаем все ведущие пробелы)
    const auto start = input.find_first_not_of(spaces);
    if (start == std::string::npos) {
        // Нет ни одного непробельного символа
        input.clear();
        return std::nullopt;
    }

    // Поиск конца слова (первый пробельный символ после start)
    const auto end = input.find_first_of(spaces, start);

    // Получаем первое слово
    const auto word = input.substr(start, (end == std::string::npos ? input.size() : end) - start);

    // Обрезаем строку до первого значащего символа
    const auto next
        = (end == std::string::npos) ? std::string::npos : input.find_first_not_of(spaces, end);
    if (next == std::string::npos) {
        input.clear();
    }
    else {
        input.erase(0, next);
    }

    return word;
}

/**
 * @brief Удаление незначащих символов в конце строки
 * @param input Ссылка на исходную строку
 */
void rtrim(std::string &input)
{
    // Множество пробельных символов (аналогично std::isSpace())
    constexpr char spaces[] = " \t\n\r\f\v";
    const auto end = input.find_last_not_of(spaces);
    if (end == std::string::npos) {
        // Строка состоит только из пробелов
        input.clear();
    }
    else {
        // Обрезаем от конца последнего значащего символа
        input.erase(end + 1);
    }
}

/**
 * @brief Разбор входной строки
 * @param input Входная строка
 * @return Команда + аргументы (если строка пустая - std::nullopt)
 */
std::optional<std::pair<std::string, std::vector<std::string>>> parseInput(std::string input)
{
    // Удаляем незначащие пробелы в конце
    rtrim(input);

    if (input.empty()) {
        // Строка состояла только из пробелов
        return std::nullopt;
    }

    // Вектор для хранения аргументов
    std::vector<std::string> args;

    // Получаем строковое представление команды
    const auto command = extractFirstWord(input);
    assert(command.has_value());
    assert(command->empty() == false);

    if (input.empty()) {
        return std::make_pair(*command, args);
    }

    if (command->compare("insert") == 0) {
        // Для `insert` передаем всю оставшуюся строку в качестве аргумента
        args.push_back(std::move(input));
    }
    else if (command->compare("update") == 0) {
        // Для `update` выделяем следующее слово в отдельный аргумент (т.к. ожидаем UUID)
        const auto uuid = extractFirstWord(input);
        if (uuid.has_value()) {
            assert(uuid->empty() == false);
            args.push_back(std::move(*uuid));
        }
        // Затем передаем всю оставшуюся строку (если что-то осталось) в качестве аргумента
        if (!input.empty()) {
            args.push_back(std::move(input));
        }
    }
    else {
        // Для остальных случаев разбиваем оставшуюся строку на слова
        while (true) {
            const auto word = extractFirstWord(input);
            if (word.has_value()) {
                assert(word->empty() == false);
                args.push_back(std::move(*word));
            }
            else {
                break;
            }
        }
    }

    return std::make_pair(*command, args);
}
} // namespace

namespace octet::cli {
CommandProcessor::CommandProcessor(octet::StorageManager &storage, bool singleShotMode)
    : storage_(storage)
    , singleShotMode_(singleShotMode)
{
    // Регистрация всех доступных команд

    // Команда вставки строки
    commands_["insert"]
        = { 1, false,
            [this](const std::vector<std::string> &args, std::ostream &out) -> CommandResult {
                const auto result = storage_.insert(args[0]);
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
                const auto result = storage_.get(args[0]);
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
                const auto result = storage_.update(args[0], args[1]);
                return result ? CommandResult::SUCCESS : CommandResult::FAILURE;
            } };

    // Команда удаления строки
    commands_["remove"]
        = { 1, false,
            [this](const std::vector<std::string> &args, std::ostream &) -> CommandResult {
                const auto result = storage_.remove(args[0]);
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
                   const auto threshold = std::stoul(args[0]);
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
                   const auto minutes = std::stoul(args[0]);
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
    commands_["help"] = {
        0, true,
        [this](const std::vector<std::string> &, std::ostream &out) -> CommandResult {
            out << "Доступные команды:\n"
                << "  insert <СТРОКА>              Вставить строку и получить ее UUID\n"
                << "  get <UUID>                   Получить строку по UUID\n"
                << "  update <UUID> <СТРОКА>       Обновить строку по UUID\n"
                << "  remove <UUID>                Удалить строку по UUID\n"
                << "  snapshot                     Принудительно создать снапшот\n"
                << "  set-snapshot-operations <N>  Изменить порог операций для снапшота\n"
                << "  set-snapshot-minutes <N>     Изменить интервал снапшота в минутах\n"
                << "  exit                         Выход из интерактивного режима\n"
                << "  help                         Показать справку по доступным командам\n\n"

                << "  В этом режиме <СТРОКА> интерпретируется как есть — она принимается целиком,\n"
                << "  без разбиения на слова или анализa содержимого. Перед обработкой из строки\n"
                << "  удаляются только незначащие пробелы в начале и в конце.\n\n";
            return CommandResult::SUCCESS;
        }
    };
}

CommandResult CommandProcessor::do_execute(const std::string &command,
                                           std::vector<std::string> args, std::ostream &out) const
{
    auto it = commands_.find(command);
    if (it == commands_.end()) {
        LOG_ERROR << "Ошибка: неизвестная команда: " << command << ".\n"
                  << "Введите `help` для получения списка доступных команд";
        return CommandResult::FAILURE;
    }

    if (command.compare("insert") == 0) {
        // Для `insert` все элементы вектора схлопываем в первый
        mergeVector(args, 0);
    }
    else if (command.compare("update") == 0) {
        // Для `update` все элементы вектора схлопываем во второй
        mergeVector(args, 1);
    }

    const auto &cmd = it->second;
    // Проверка количества аргументов
    if (args.size() != cmd.argsCount) {
        LOG_ERROR << "Ошибка: неправильное использование команды " << command << ".\n"
                  << "Введите `help` для получения информации об использовании команд";
        return CommandResult::FAILURE;
    }

    // Проверка возможности выполнения команды
    if (singleShotMode_ && cmd.onlyForInteractive) {
        LOG_ERROR << "Ошибка: команда " << command << " доступна только в интерактивном режиме.\n"
                  << "Введите `help` для получения информации об использовании команд";
        return CommandResult::FAILURE;
    }

    // Выполнение команды
    return cmd.execute(args, out);
}

CommandResult CommandProcessor::executeShot(StorageManager &storage, std::vector<std::string> args)
{
    if (args.empty()) {
        LOG_ERROR << "Ошибка: необходимо указать команду для выполнения.\n"
                  << "Введите `help` для получения списка доступных команд";
        return CommandResult::FAILURE;
    }

    // Достаем команду из аргументов
    const auto command = std::move(args.front());
    args.erase(args.begin());

    const auto processor = CommandProcessor(storage, true);
    return processor.do_execute(command, std::move(args));
}

int CommandProcessor::runInteractiveMode(StorageManager &storage)
{
    const auto processor = CommandProcessor(storage, false);

    constexpr char PROMT[] = "octet> ";
    std::string input;

    // Приветственное сообщение
    std::cout << "Octet - интерактивный режим\n"
              << "Введите команду или 'help' для получения справки, 'exit' для выхода\n";

    while (true) {
        // Вывод приглашения и получение ввода
        std::cout << PROMT;
        std::getline(std::cin, input);

        // Проверка на ошибку ввода или EOF
        if (!std::cin) {
            LOG_ERROR << "Ошибка ввода. Завершение работы.\n";
            return 1;
        }

        // Пропускаем пустые строки
        if (input.empty()) {
            continue;
        }

        const auto parsedData = parseInput(std::move(input));
        if (!parsedData.has_value()) {
            // Скорее всего строка состояла только из пробелов
            continue;
        }
        auto [command, args] = std::move(*parsedData);

        // Выполняем команду
        try {
            const auto result = processor.do_execute(command, args);
            if (result == octet::cli::CommandResult::EXIT) {
                std::cout << "Выход из интерактивного режима\n";
                return 0;
            }
        }
        catch (const std::exception &e) {
            LOG_ERROR << "Ошибка выполнения команды: " << e.what();
        }
    }
    UNREACHABLE("Exit from the loop can only be done manually");
}
} // namespace octet::cli
