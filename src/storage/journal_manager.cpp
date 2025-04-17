#include "storage/journal_manager.hpp"

#include <cassert>
#include <chrono>
#include <array>
#include <regex>

#include "utils/compiler.hpp"
#include "utils/file_lock_guard.hpp"
#include "utils/file_utils.hpp"
#include "utils/logger.hpp"

namespace {
// Константы для форматирования журнала
static constexpr char FIELD_SEPARATOR = '|';
static constexpr char ESCAPE_CHAR = '\\';
static constexpr char JOURNAL_HEADER[] = "# OCTET Journal Format v1.0\n";

// Строковые представления типов операций
// !!! Порядок должен соответствовать порядку в перечислении OperationType
static const std::array<std::string, 4> OPERATION_TYPE_STRINGS
    = { "INSERT", "UPDATE", "REMOVE", "CHECKPOINT" };

/**
 * @brief Экранирует специальные символы в строке для хранения в журнале
 * @param str Исходная строка
 * @return Экранированная строка
 */
std::string escapeString(const std::string &str)
{
    std::ostringstream result;
    for (char c : str) {
        if (c == FIELD_SEPARATOR || c == ESCAPE_CHAR) {
            // Обычное экранирование для разделителей и экранирующих символов
            result << ESCAPE_CHAR << c;
        }
        else if (c == '\n') {
            // Заменяем символ новой строки на строковое представление "\n"
            result << ESCAPE_CHAR << 'n';
        }
        else if (c == '\r') {
            // Заменяем символ возврата каретки на строковое представление "\r"
            result << ESCAPE_CHAR << 'r';
        }
        else {
            // Обычные символы просто копируем
            result << c;
        }
    }
    return result.str();
}

/**
 * @brief Убирает экранирование специальных символов в строке
 * @param str Экранированная строка
 * @return Исходная строка без экранирования
 */
std::string unescapeString(const std::string &str)
{
    std::ostringstream result;
    bool escaped = false;
    for (char c : str) {
        if (escaped) {
            if (c == 'n') {
                // Преобразуем "\n" обратно в символ новой строки
                result << '\n';
            }
            else if (c == 'r') {
                // Преобразуем "\r" обратно в символ возврата каретки
                result << '\r';
            }
            else {
                // Все остальные экранированные символы добавляем как есть
                result << c;
            }
            escaped = false;
        }
        else if (c == ESCAPE_CHAR) {
            escaped = true;
        }
        else {
            result << c;
        }
    }
    return result.str();
}

/**
 * @brief Форматирует строку ISO 8601 из текущего времени
 * @return Строка с временной меткой
 */
std::string getCurrentIsoTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0')
        << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

/**
 * @brief Преобразование типа операции в строку
 * @return Операция в строковом формате
 */
std::string operationTypeToString(octet::OperationType type)
{
    const auto index = static_cast<size_t>(type);
    if (index < OPERATION_TYPE_STRINGS.size()) {
        return OPERATION_TYPE_STRINGS[index];
    }
    UNREACHABLE("Unsupported OperationType");
}

/**
 * @brief Преобразование строки в тип операции
 * @return Операция в формате OperationType
 */
std::optional<octet::OperationType> stringToOperationType(const std::string &typeStr)
{
    for (size_t i = 0; i < OPERATION_TYPE_STRINGS.size(); i++) {
        if (typeStr == OPERATION_TYPE_STRINGS[i]) {
            return static_cast<octet::OperationType>(i);
        }
    }
    return std::nullopt;
}

/**
 * @brief Чтение строки из содержимого всего журнала
 * @param content Содержимое всего журнала
 * @param pos Начальная позиция чтения
 * @return Строка из журнала или std::nullopt при ошибке чтения или если строка пустая или является
 * комментарием
 */
std::optional<std::string> readLineFromJournalContent(const std::string &content, size_t &pos)
{
    const auto contentSize = content.size();
    if (pos >= contentSize) {
        LOG_ERROR << "Ошибка при чтении строки: индекс стартовой позиции чтения (" << pos
                  << ") выходит за рамки размера контента (" << contentSize << ")";
        return std::nullopt;
    }

    // Ищем конец строки
    const auto nextPos = content.find('\n', pos);
    // Если конец строки не найден, обрабатываем до конца строки
    const auto endlFound = nextPos != std::string::npos;
    const auto lineLength = endlFound ? (nextPos - pos) : (contentSize - pos);

    // Используем string_view для избежания лишних копирований
    std::string_view currentLine(content.data() + pos, lineLength);

    // Обновляем позицию для следующей итерации
    pos = endlFound ? (nextPos + 1) : contentSize;

    // Пропускаем пустые строки и комментарии
    if (currentLine.empty() || currentLine[0] == '#') {
        return std::nullopt;
    }

    return std::string(currentLine);
}
} // namespace

namespace octet {
JournalEntry::JournalEntry(OperationType type, std::string uuid, std::string data,
                           std::string timestamp)
    : type_(type)
    , uuid_(std::move(uuid))
    , data_(std::move(data))
    , timestamp_(timestamp.empty() ? getCurrentIsoTimestamp() : std::move(timestamp))
{
}

std::string JournalEntry::serialize() const
{
    std::ostringstream oss;
    // Формат: TYPE|GUID|TIMESTAMP|DATA
    oss << operationTypeToString(type_) << FIELD_SEPARATOR << uuid_ << FIELD_SEPARATOR << timestamp_
        << FIELD_SEPARATOR << escapeString(data_) << "\n";
    return oss.str();
}

std::optional<JournalEntry> JournalEntry::deserialize(const std::string &line)
{
    // Регулярное выражение для проверки формата строки журнала
    // Формат: TYPE|GUID|TIMESTAMP|DATA (где DATA может быть пустым)
    static const std::regex JOURNAL_ENTRY_REGEX(
        R"(^(INSERT|UPDATE|REMOVE|CHECKPOINT)\|([^|]+)\|([^|]+)\|(.*)$)");
    std::smatch matches;
    if (!std::regex_match(line, matches, JOURNAL_ENTRY_REGEX)) {
        return std::nullopt;
    }
    const auto type = stringToOperationType(matches[1]);
    if (!type.has_value()) {
        return std::nullopt;
    }
    const auto uuid = matches[2];
    const auto timestamp = matches[3];
    const auto data = unescapeString(matches[4]);
    return JournalEntry(*type, uuid, data, timestamp);
}

JournalManager::JournalManager(const std::filesystem::path &journalPath)
    : journalFilePath_(journalPath)
    , lastCheckpointId_(std::nullopt)
{
    LOG_INFO << "Инициализация журнала по пути: " << journalFilePath_.string();

    bool needToRecrate = false;
    // Проверяем, существует ли файл журнала, и создаем его (вместе с директориями), если нет
    if (!utils::checkIfFileExists(journalFilePath_)) {
        LOG_INFO << "Файл журнала не найден, создаем новый: " << journalFilePath_.string();
        needToRecrate = true;
    }
    // Проверяем валидность журнала
    else if (!isJournalValid()) {
        LOG_WARNING << "Формат журнала некорректен, создаем резервную копию и новый журнал";
        needToRecrate = true;

        // Создаем резервную копию
        auto backupPath = utils::createFileBackup(journalFilePath_);
        if (backupPath.has_value()) {
            LOG_INFO << "Создана резервная копия поврежденного журнала: " << backupPath->string();
        }
        else {
            LOG_CRITICAL << "Не удалось создать резервную копию поврежденного журнала: "
                         << journalFilePath_.string() << ", прерываем, чтобы не повредить данные";
            throw std::runtime_error("JournalManager: не удалось создать новый журнал "
                                     + journalFilePath_.string());
            return;
        }
    }

    if (needToRecrate) {
        // Создаем новый файл журнала
        const std::string header = JOURNAL_HEADER;
        if (!utils::atomicFileWrite(journalFilePath_, header)) {
            LOG_CRITICAL << "Не удалось создать новый файл журнала: " << journalFilePath_.string();
            throw std::runtime_error("JournalManager: не удалось создать новый журнал "
                                     + journalFilePath_.string());
        }
    }
}

JournalManager::~JournalManager()
{
    LOG_DEBUG << "Закрытие журнала: " << journalFilePath_.string();
}

bool JournalManager::writeOperation(OperationType opType, const std::string &uuid,
                                    const std::string &data)
{
    LOG_DEBUG << "Запись операции в журнал: " << journalFilePath_.string()
              << ", операция = " << operationTypeToString(opType);

    // Идентификатор должен быть обязательно не пустым
    if (uuid.empty()) {
        LOG_ERROR << "Попытка записи операции с пустым UUID";
        return false;
    }

    // Если это контрольная точка, обновляем кэшированное значение
    bool writeResult = false;
    if (opType == OperationType::CHECKPOINT) {
        // Используем блокировку для поддержания атомарности между записью в файл и обновлением кэша
        std::lock_guard<std::mutex> lock(journalMutex_);
        writeResult = do_writeOperation(opType, uuid, data);
        if (writeResult) {
            // Обновляем кэшированное значение только при успешном добавлении операции в журнал
            lastCheckpointId_ = uuid;
        }
    }
    else {
        writeResult = do_writeOperation(opType, uuid, data);
    }
    return writeResult;
}

bool JournalManager::do_writeOperation(OperationType opType, const std::string &uuid,
                                       const std::string &data)
{
    // Сериализуем запись
    JournalEntry entry(opType, uuid, data);
    const auto serializedEntry = entry.serialize();
    // Выполняем запись операции
    const auto writeResult = utils::safeFileAppend(journalFilePath_, serializedEntry);
    if (!writeResult) {
        LOG_ERROR << "Не удалось записать операцию в журнал, тип: " << operationTypeToString(opType)
                  << ", UUID: " << uuid;
    }
    return writeResult;
}

bool JournalManager::writeInsert(const std::string &uuid, const std::string &data)
{
    return writeOperation(OperationType::INSERT, uuid, data);
}

bool JournalManager::writeUpdate(const std::string &uuid, const std::string &data)
{
    return writeOperation(OperationType::UPDATE, uuid, data);
}

bool JournalManager::writeRemove(const std::string &uuid)
{
    return writeOperation(OperationType::REMOVE, uuid);
}

bool JournalManager::writeCheckpoint(const std::string &snapshotId)
{
    return writeOperation(OperationType::CHECKPOINT, snapshotId);
}

bool JournalManager::replayJournal(std::unordered_map<std::string, std::string> &dataStore,
                                   const std::optional<std::string> &lastCheckpoint)
{
    LOG_DEBUG << "Воспроизведение действий из журнала: " << journalFilePath_.string()
              << ", начиная с контрольной точки: "
              << (lastCheckpoint.has_value() ? *lastCheckpoint : "[нет]");

    if (lastCheckpoint.has_value() && lastCheckpoint->empty()) {
        LOG_ERROR << "Указан пустой идентификатор контрольной точки";
        return false;
    }

    std::string journalContent;
    if (!utils::safeFileRead(journalFilePath_, journalContent)) {
        LOG_ERROR << "Не удалось прочитать файл журнала: " << journalFilePath_.string();
        return false;
    }

    const auto applyFromCheckpoint = lastCheckpoint.has_value();
    const auto checkpoint = applyFromCheckpoint ? *lastCheckpoint : "";
    // Флаг, указывающий, нашли ли мы указанную контрольную точку
    bool foundCheckpoint = false;

    // Счетчики операций
    size_t totalOperations = 0;
    size_t appliedOperations = 0;

    size_t pos = 0;
    while (pos < journalContent.size()) {
        // Получаем текущую строку
        const auto line = readLineFromJournalContent(journalContent, pos);
        // Пропускаем пустые строки и комментарии
        if (!line.has_value()) {
            continue;
        }

        // Десериализуем запись
        const auto entry = JournalEntry::deserialize(*line);
        if (!entry.has_value()) {
            LOG_ERROR << "Некорректный формат записи в журнале: " << *line;
            continue;
        }
        totalOperations++;

        if (applyFromCheckpoint) {
            // Если это контрольная точка, проверяем, не это ли искомая точка
            if (entry->type() == OperationType::CHECKPOINT) {
                if (entry->uuid() == checkpoint) {
                    // Для отладки проверяем, что такая контрольная точка уникальна
                    assert(foundCheckpoint == false);
                    foundCheckpoint = true;
                    LOG_INFO << "Найдена контрольная точка: " << *lastCheckpoint;
                }
                continue;
            }

            // Пропускаем операции до нахождения контрольной точки
            if (!foundCheckpoint) {
                continue;
            }
        }

        // Применяем операцию к хранилищу
        if (applyOperation(*entry, dataStore)) {
            appliedOperations++;
        }
        else {
            LOG_ERROR << "Не удалось применить операцию: " << *line;
        }
    }

    LOG_INFO << "Воспроизведение журнала завершено: " << journalFilePath_.string()
             << ", всего операций = " << totalOperations << ", применено: " << appliedOperations;

    if (applyFromCheckpoint && !foundCheckpoint) {
        LOG_WARNING << "Контрольная точка не найдена в журнале: " << journalFilePath_.string()
                    << ", точка = " << *lastCheckpoint;
        return false;
    }

    return true;
}

// Получение последнего идентификатора контрольной точки
std::optional<std::string> JournalManager::getLastCheckpointId() const
{
    // Для поддержания атомарности между чтением журнала и обновления контрольной точки
    std::lock_guard<std::mutex> lock(journalMutex_);

    LOG_DEBUG << "Получение последней контрольной точки из журнала: " << journalFilePath_.string();

    // Если у нас есть кэшированное значение, используем его
    if (lastCheckpointId_.has_value()) {
        return lastCheckpointId_;
    }

    // Иначе считываем журнал и ищем последнюю контрольную точку
    std::optional<std::string> newCheckpointId;

    std::string journalContent;
    if (!utils::safeFileRead(journalFilePath_, journalContent)) {
        LOG_ERROR << "Не удалось прочитать файл журнала: " << journalFilePath_.string();
        return std::nullopt;
    }

    size_t pos = 0;
    while (pos < journalContent.size()) {
        // Получаем текущую строку
        const auto line = readLineFromJournalContent(journalContent, pos);
        // Пропускаем пустые строки и комментарии
        if (!line.has_value()) {
            continue;
        }

        // Десериализуем запись
        const auto entry = JournalEntry::deserialize(*line);
        if (entry.has_value() && entry->type() == OperationType::CHECKPOINT) {
            newCheckpointId = entry->uuid();
        }
    }
    lastCheckpointId_ = newCheckpointId;

    LOG_DEBUG << "Последняя найденная контрольная точка из журнала: " << journalFilePath_.string()
              << ", точка = " << (lastCheckpointId_.has_value() ? *lastCheckpointId_ : "[нет]");

    return newCheckpointId;
}

// Очистка журнала до определенной контрольной точки
bool JournalManager::truncateJournalToCheckpoint(const std::string &checkpointId)
{
    if (checkpointId.empty()) {
        LOG_ERROR << "Попытка очистки журнала с пустым UUID контрольной точки";
        return false;
    }

    LOG_INFO << "Очистка журнала до контрольной точки: " << journalFilePath_.string()
             << ", точка = " << checkpointId;

    // Чтобы обеспечить безопасность очистки журнала в многопоточной и межпроцессорных средах
    // используем файловую блокировку, но с именем, отличным от стандартного генерируемого имени
    // блокировки
    // TODO: изменить наименование файла блокировки
    const auto truncateLockFile = std::filesystem::path(journalFilePath_.string() + ".truncate");
    utils::FileLockGuard lock(truncateLockFile);
    if (!lock.isLocked()) {
        LOG_ERROR << "Не удалось получить блокировку для очистки журнала: "
                  << journalFilePath_.string();
        return false;
    }

    // Читаем все записи из журнала начиная с указанной контрольной точки
    std::vector<JournalEntry> allEntries;
    if (!readAllEntriesFrom(checkpointId, allEntries)) {
        LOG_ERROR << "Не удалось прочитать записи для очистки журнала";
        return false;
    }

    // Ищем индекс нужной контрольной точки
    std::optional<size_t> checkpointIndex = std::nullopt;
    for (size_t i = 0; i < allEntries.size(); i++) {
        const auto &entry = allEntries[i];
        if (entry.type() == OperationType::CHECKPOINT && entry.uuid() == checkpointId) {
            checkpointIndex = i;
            break;
        }
    }

    if (!checkpointIndex.has_value()) {
        LOG_ERROR << "Контрольная точка не найдена в журнале: " << journalFilePath_.string()
                  << ", точка = " << checkpointId;
        return false;
    }

    // Очищаем все элементы в векторе до найденной контрольной точки (не включительно)
    allEntries.erase(allEntries.begin(), allEntries.begin() + *checkpointIndex);

    // Перезаписываем журнал
    if (!rewriteJournal(allEntries)) {
        LOG_ERROR << "Не удалось перезаписать журнал после очистки: " << journalFilePath_.string();
        return false;
    }

    LOG_INFO << "Журнал успешно очищен: " << journalFilePath_.string() << ", удалено "
             << *checkpointIndex << " записей";
    return true;
}

// Подсчет операций после последней контрольной точки
std::optional<size_t> JournalManager::countOperationsSinceLastCheckpoint() const
{
    // Так как выполняем сразу две потенциально "опасные" операции (чтение для получения последней
    // контрольной точки и чтение для подсчета операций), то ставим файловую блокировку
    // TODO: изменить наименование файла блокировки
    const auto countLockFile = std::filesystem::path(journalFilePath_.string() + ".count");
    utils::FileLockGuard lock(countLockFile);
    if (!lock.isLocked()) {
        LOG_ERROR << "Не удалось получить блокировку для очистки журнала: "
                  << journalFilePath_.string();
        return false;
    }

    // Обновляем последнюю контрольную точку
    getLastCheckpointId();

    // Читаем все записи из журнала начиная с указанной контрольной точки
    std::vector<JournalEntry> allEntries;
    if (!readAllEntriesFrom(lastCheckpointId_, allEntries)) {
        LOG_ERROR << "Не удалось прочитать записи для очистки журнала";
        return false;
    }

    // Получаем количество операций
    const auto operationCount = allEntries.size();
    LOG_DEBUG << "Количество операций после последней контрольной точки в журнале: "
              << journalFilePath_.string() << ", количество = " << operationCount;
    return operationCount;
}

bool JournalManager::isJournalValid() const
{
    LOG_DEBUG << "Проверка валидности журнала: " << journalFilePath_.string();

    // Проверяем существование файла (без создания родительских директорий)
    if (!utils::checkIfFileExists(journalFilePath_, false)) {
        LOG_DEBUG << "Файл журнала не существует";
        return false;
    }

    std::string journalContent;
    if (!utils::safeFileRead(journalFilePath_, journalContent)) {
        LOG_ERROR << "Не удалось прочитать файл журнала: " << journalFilePath_.string();
        return false;
    }

    size_t pos = 0;
    while (pos < journalContent.size()) {
        // Получаем текущую строку
        const auto line = readLineFromJournalContent(journalContent, pos);
        // Пропускаем пустые строки и комментарии
        if (!line.has_value()) {
            continue;
        }

        // Проверяем формат строки
        if (!JournalEntry::deserialize(*line).has_value()) {
            LOG_WARNING << "Неверный формат строки в журнале: " << *line;
            return false;
        }
    }

    LOG_INFO << "Журнал прошел проверку валидности";
    return true;
}

// Применение операции к хранилищу
bool JournalManager::applyOperation(const JournalEntry &entry,
                                    std::unordered_map<std::string, std::string> &dataStore) const
{
    switch (entry.type()) {
    case OperationType::INSERT: {
        dataStore[entry.uuid()] = entry.data();
        LOG_DEBUG << "Применена операция INSERT для UUID: " << entry.uuid();
        return true;
    }
    case OperationType::UPDATE: {
        const auto uuid = entry.uuid();
        // Проверяем существование записи
        if (dataStore.find(uuid) == dataStore.end()) {
            LOG_ERROR << "Операция UPDATE для несуществующего UUID: " << uuid;
            return false;
        }
        dataStore[uuid] = entry.data();
        LOG_DEBUG << "Применена операция UPDATE для UUID: " << uuid;
        return true;
    }
    case OperationType::REMOVE: {
        const auto uuid = entry.uuid();
        // Проверяем существование записи
        if (dataStore.find(uuid) == dataStore.end()) {
            LOG_WARNING << "Операция REMOVE для несуществующего UUID: " << uuid;
            return false;
        }
        dataStore.erase(uuid);
        LOG_DEBUG << "Применена операция REMOVE для UUID: " << uuid;
        return true;
    }
    case OperationType::CHECKPOINT:
        // Контрольные точки не применяются к хранилищу
        return true;
    }
    UNREACHABLE("Unsupported OperationType");
}

bool JournalManager::readAllEntriesFrom(const std::optional<std::string> &checkpointId,
                                        std::vector<JournalEntry> &entries) const
{
    LOG_DEBUG << "Считывание записей из журнала: " << journalFilePath_.string();

    if (checkpointId.has_value() && checkpointId->empty()) {
        LOG_ERROR << "Указан пустой идентификатор контрольной точки";
        return false;
    }

    std::string journalContent;
    if (!utils::safeFileRead(journalFilePath_, journalContent)) {
        LOG_ERROR << "Не удалось прочитать файл журнала: " << journalFilePath_.string();
        return false;
    }

    const auto readFromCheckpoint = checkpointId.has_value();
    const auto checkpoint = readFromCheckpoint ? *checkpointId : "";
    bool foundCheckpoint = false;

    size_t pos = 0;
    while (pos < journalContent.size()) {
        // Получаем текущую строку
        const auto line = readLineFromJournalContent(journalContent, pos);
        // Пропускаем пустые строки и комментарии
        if (!line.has_value()) {
            continue;
        }

        // Десериализуем запись
        const auto entry = JournalEntry::deserialize(*line);
        if (!entry.has_value()) {
            LOG_WARNING << "Некорректная запись в журнале: " << *line;
            continue;
        }

        if (readFromCheckpoint) {
            // Если это контрольная точка, проверяем, не это ли искомая точка
            if (entry->type() == OperationType::CHECKPOINT) {
                if (entry->uuid() == checkpoint) {
                    // Для отладки проверяем, что такая контрольная точка уникальна
                    assert(foundCheckpoint == false);
                    foundCheckpoint = true;
                }
                continue;
            }

            // Пропускаем операции до нахождения контрольной точки
            if (!foundCheckpoint) {
                continue;
            }
        }
        entries.push_back(*entry);
    }
    return true;
}

// Перезапись журнала с новым набором записей
bool JournalManager::rewriteJournal(const std::vector<JournalEntry> &entries)
{
    LOG_DEBUG << "Перезапись журнала с новым набором записей, журнал: "
              << journalFilePath_.string();

    std::optional<std::string> newCheckpointId = std::nullopt;

    // Создаем новое содержимое журнала
    std::ostringstream oss;
    oss << JOURNAL_HEADER;
    for (const auto &entry : entries) {
        // Если в новых записях есть контрольная точка, обновляем кэшированное значение
        if (entry.type() == OperationType::CHECKPOINT) {
            newCheckpointId = entry.uuid();
        }
        oss << entry.serialize() << "\n";
    }

    // Записываем новое содержимое
    const auto rewriteResult = utils::atomicFileWrite(journalFilePath_, oss.str());
    if (rewriteResult) {
        LOG_DEBUG << "Успешно перезаписан журнал: " << journalFilePath_.string();
        lastCheckpointId_ = newCheckpointId;
    }
    else {
        LOG_ERROR << "Не удалось перезаписать журнал: " << journalFilePath_.string();
    }
    return rewriteResult;
}
} // namespace octet
