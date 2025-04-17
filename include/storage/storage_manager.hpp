#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <optional>

#include "storage/journal_manager.hpp"
#include "storage/uuid_generator.hpp"
#include "utils/logger.hpp"

namespace octet {
/**
 * @class StorageManager
 * @brief Управляет хранением UTF-8 строк и их идентификаторов.
 *
 * Реализует гибридное хранилище с данными в памяти и постоянное хранение на диске.
 * Обеспечивает базовые операции вставки, получения, обновления и удаления.
 */
class StorageManager {
public:
    /**
     * @brief Конструктор с указанием путей к файлам хранилища
     * @param dataDir Директория для хранения файлов
     */
    explicit StorageManager(const std::filesystem::path &dataDir);

    /**
     * @brief Деструктор, гарантирующий корректное закрытие ресурсов
     */
    ~StorageManager();

    // Запрещаем копирование и перемещение
    StorageManager(const StorageManager &) = delete;
    StorageManager &operator=(const StorageManager &) = delete;
    StorageManager(StorageManager &&) = delete;
    StorageManager &operator=(StorageManager &&) = delete;

    /**
     * @brief Добавляет UTF-8 строку в хранилище
     * @param data Строка данных для сохранения
     * @return UUID для добавленной строки или std::nullopt при ошибке
     */
    std::optional<std::string> insert(const std::string &data);

    /**
     * @brief Извлекает строку по её идентификатору
     * @param uuid Уникальный идентификатор строки
     * @return Сохранённая строка данных или std::nullopt, если не найдена
     */
    std::optional<std::string> get(const std::string &uuid) const;

    /**
     * @brief Обновляет существующую строку новыми данными
     * @param uuid Уникальный идентификатор строки для обновления
     * @param data Новые данные для сохранения
     * @return true если обновление выполнено успешно или false при ошибке
     */
    bool update(const std::string &uuid, const std::string &data);

    /**
     * @brief Удаляет строку из хранилища
     * @param uuid Уникальный идентификатор строки для удаления
     * @return true если удаление выполнено успешно или false при ошибке
     */
    bool remove(const std::string &uuid);

    /**
     * @brief Явно создаёт снимок текущего состояния хранилища
     * @return true если снимок создан успешно
     */
    bool createSnapshot();

    /**
     * @brief Принудительно запрашивает асинхронное создание снапшота
     */
    void requestSnapshotAsync();

    /**
     * @brief Возвращает количество записей в хранилище
     * @return Количество записей
     */
    size_t getEntriesCount() const;

    /**
     * @brief Задает порог операций для автоматического создания снапшота
     * @param threshold Количество операций (по умолчанию: 100 операций)
     */
    void setSnapshotOperationsThreshold(size_t threshold);

    /**
     * @brief Задает интервал времени для автоматического создания снапшота
     * @param minutes Интервал в минутах (по умолчанию: 10 минут)
     */
    void setSnapshotTimeThreshold(size_t minutes);

private:
    // Хранилище данных в памяти
    std::unordered_map<std::string, std::string> dataStore_;

    const std::filesystem::path dataDir_;
    const std::filesystem::path snapshotPath_;

    JournalManager journalManager_;
    UuidGenerator uuidGenerator_;

    // Мьютекс для синхронизации (разделяемый для повышения производительности чтения)
    mutable std::shared_mutex storageMutex_;

    // Параметры снапшотов
    std::atomic<size_t> operationsSinceLastSnapshot_{ 0 };
    std::atomic<size_t> snapshotOperationsThreshold_{ 100 }; // По умолчанию каждые 100 операций
    std::atomic<size_t> snapshotTimeThresholdMinutes_{ 10 }; // По умолчанию каждые 10 минут

    // Для управления асинхронными снапшотами
    std::thread snapshotThread_;
    std::condition_variable snapshotCondition_;
    std::mutex snapshotMutex_;
    std::atomic<bool> shutdownRequested_{ false };
    std::atomic<bool> snapshotRequested_{ false };
    std::chrono::steady_clock::time_point lastSnapshotTime_;

    /**
     * @brief Загружает данные из файлов в память
     * @return true если загрузка выполнена успешно
     */
    bool loadFromDisk();

    /**
     * @brief Загружает данные из снапшота
     * @return true если загрузка выполнена успешно
     */
    bool loadSnapshot();

    /**
     * @brief Восстанавливает данные из журнала операций
     * @param lastCheckpointId ID последней контрольной точки (опционально)
     * @return true если восстановление выполнено успешно
     */
    bool restoreFromJournal(const std::optional<std::string> &lastCheckpointId = std::nullopt);

    /**
     * @brief Записывает снапшот на диск
     * @param data Данные для записи
     * @return true если запись выполнена успешно
     */
    bool writeSnapshotToDisk(const std::unordered_map<std::string, std::string> &data);

    /**
     * @brief Функция потока для создания снапшотов.
     */
    void snapshotThreadFunction();

    /**
     * @brief Уведомляет о выполнении операции, изменяющей данные.
     */
    void notifyOperation();
};

} // namespace octet
