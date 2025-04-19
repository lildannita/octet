#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace octet {
/**
 * @enum OperationType
 * @brief Типы операций для журнала.
 */
enum class OperationType : uint8_t {
    INSERT, // Добавление новой строки
    UPDATE, // Обновление существующей строки
    REMOVE, // Удаление строки
    CHECKPOINT // Контрольная точка (снимок состояния)
};

/**
 * @struct JournalEntry
 * @brief Структура для хранения записи в журнале
 */
struct JournalEntry {
    /**
     * @brief Конструктор записи журнала
     * @param type Тип операции
     * @param uuid Идентификатор строки
     * @param data Данные операции
     * @param timestamp Временная метка (если не указана, будет сгенерирована)
     */
    JournalEntry(OperationType type, std::string uuid, std::string data = "",
                 std::string timestamp = "");
    /**
     * @brief Сериализует запись журнала в строку для записи в файл
     * @param entry Запись журнала для сериализации
     * @return Строка, представляющая запись в формате журнала
     */
    std::string serialize() const;

    /**
     * @brief Десериализует строку из файла журнала в запись журнала
     * @param line Строка из журнала
     * @return Запись журнала или std::nullopt в случае ошибки формата
     */
    static std::optional<JournalEntry> deserialize(const std::string &line);

    /**
     * @brief Геттер для типа операции
     * @return Тип операции
     */
    OperationType type() const
    {
        return type_;
    }

    /**
     * @brief Геттер для идентификатора строки
     * @return Идентификатор строки
     */
    std::string uuid() const
    {
        return uuid_;
    }

    /**
     * @brief Геттер для данных операции
     * @return Данные операции
     */
    std::string data() const
    {
        return data_;
    }

private:
    OperationType type_; // Тип операции
    std::string uuid_; // Идентификатор строки
    std::string data_; // Данные операции (для INSERT и UPDATE)
    std::string timestamp_; // Временная метка операции
};

/**
 * @class JournalManager
 * @brief Управляет журналом операций и обеспечивает восстановление данных.
 *
 * Реализует механизм журналирования (WAL) для обеспечения устойчивости
 * к сбоям и возможности восстановления данных после неожиданного завершения.
 * Все операции изменения данных сначала записываются в журнал, а затем применяются
 * к хранилищу, что гарантирует согласованность при сбоях.
 */
class JournalManager {
public:
    /**
     * @brief Конструктор с указанием пути к файлу журнала
     * @param journalPath Путь к файлу журнала операций
     */
    explicit JournalManager(const std::filesystem::path &journalPath);

    /**
     * @brief Деструктор, гарантирующий закрытие ресурсов
     */
    ~JournalManager();

    /**
     * @brief Записывает операцию в журнал
     * @param opType Тип операции
     * @param uuid Идентификатор строки
     * @param data Данные операции (для INSERT и UPDATE)
     * @return true если запись выполнена успешно
     */
    bool writeOperation(OperationType opType, const std::string &uuid,
                        const std::string &data = "");

    /**
     * @brief Записывает операцию INSERT в журнал
     * @param uuid Идентификатор строки
     * @param data Данные операции
     * @return true если запись выполнена успешно
     */
    bool writeInsert(const std::string &uuid, const std::string &data = "");

    /**
     * @brief Записывает операцию UPDATE в журнал
     * @param uuid Идентификатор строки
     * @param data Данные операции
     * @return true если запись выполнена успешно
     */
    bool writeUpdate(const std::string &uuid, const std::string &data = "");

    /**
     * @brief Записывает операцию REMOVE в журнал
     * @param uuid Идентификатор строки
     * @return true если запись выполнена успешно
     */
    bool writeRemove(const std::string &uuid);

    /**
     * @brief Создаёт запись контрольной точки в журнале
     * @param snapshotId Идентификатор снимка состояния
     * @return true если запись выполнена успешно
     */
    bool writeCheckpoint(const std::string &snapshotId);

    /**
     * @brief Воспроизводит операции из журнала для восстановления данных
     * @param dataStore Ссылка на хранилище данных для восстановления
     * @param lastCheckpoint Идентификатор последней контрольной точки (опционально)
     * @return true если восстановление выполнено успешно
     */
    bool replayJournal(std::unordered_map<std::string, std::string> &dataStore,
                       const std::optional<std::string> &lastCheckpoint = std::nullopt);

    /**
     * @brief Получает последний идентификатор контрольной точки из журнала
     * @return Идентификатор последней контрольной точки или std::nullopt, если контрольных точек
     * нет
     */
    std::optional<std::string> getLastCheckpointId() const;

    /**
     * @brief Очищает журнал до определенной контрольной точки
     * @param checkpointId Идентификатор контрольной точки, до которой нужно очистить журнал
     * @return true если очистка выполнена успешно
     */
    bool truncateJournalToCheckpoint(const std::string &checkpointId);

    /**
     * @brief Подсчитывает количество операций в журнале после последней контрольной точки
     * @return Количество операций после последней контрольной точки или std::nullopt, если подсчет
     * провалился
     */
    std::optional<size_t> countOperationsSinceLastCheckpoint() const;

    /**
     * @brief Проверяет, существует ли файл журнала и корректен ли его формат
     * @return true если журнал существует и корректен
     */
    bool isJournalValid() const;

private:
    // Путь к файлу журнала
    const std::filesystem::path journalFilePath_;

    // Мьютекс для синхронизации доступа к журналу
    mutable std::mutex journalMutex_;

    // Кэшированный последний checkpoint ID для быстрого доступа
    mutable std::optional<std::string> lastCheckpointId_;

    /**
     * @brief Выполняет фактическую запись операции в журнал
     * @param opType Тип операции
     * @param uuid Идентификатор строки
     * @param data Данные операции (для INSERT и UPDATE)
     * @return true если запись выполнена успешно
     */
    bool do_writeOperation(OperationType opType, const std::string &uuid,
                           const std::string &data = "");

    /**
     * @brief Применяет операцию к хранилищу данных
     * @param entry Запись журнала с операцией
     * @param dataStore Хранилище данных для применения операции
     * @return true если операция применена успешно
     */
    bool applyOperation(const JournalEntry &entry,
                        std::unordered_map<std::string, std::string> &dataStore) const;

    /**
     * @brief Считывает все записи из журнала
     * @param checkpointId Идентификатор контрольной точки, с которой нужно начать считывание
     * (опционально)
     * @param[out] entries Записи из журнала
     * @return true если считывание выполнено успешно
     */
    bool readAllEntriesFrom(const std::optional<std::string> &checkpointId,
                            std::vector<JournalEntry> &entries) const;

    /**
     * @brief Перезаписывает журнал с новым набором записей
     * @param entries Вектор записей для перезаписи
     * @return true если перезапись выполнена успешно
     */
    bool rewriteJournal(const std::vector<JournalEntry> &entries);
};
} // namespace octet
