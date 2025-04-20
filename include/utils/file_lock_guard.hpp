#pragma once

#include <filesystem>
#include <chrono>

namespace octet::utils {
/**
 * @enum LockMode
 * @brief Режимы блокировки
 */
enum class LockMode {
    EXCLUSIVE, // Эксклюзивная блокировка
    SHARED, // Разделяемая блокировка
};

/**
 * @enum LockWaitStrategy
 * @brief Стратегии ожидания при попытке получения блокировки
 */
enum class LockWaitStrategy {
    STANDARD, // Стандартная стратегия (бесконечное ожидание)
    INSTANTLY, // Немедленный возврат при невозможности блокировки
    TIMEOUT, // Ожидание с таймаутом
};

/**
 * @class FileLockGuard
 * @brief Автоматически управляет блокировкой файла.
 *
 * Использует RAII-подход: блокировка создается в конструкторе и автоматически
 * освобождается в деструкторе.
 */
class FileLockGuard {
public:
    /**
     * @brief Конструктор, создает и захватывает блокировку
     * @param filePath Путь к файлу, который нужно заблокировать
     * @param mode Режим блокировки (EXCLUSIVE или SHARED)
     * @param waitStrategy Стратегия ожидания
     * @param timeout Таймаут ожидания в миллисекундах (для WAIT_WITH_TIMEOUT)
     */
    FileLockGuard(const std::filesystem::path &filePath, LockMode mode = LockMode::EXCLUSIVE,
                  LockWaitStrategy waitStrategy = LockWaitStrategy::TIMEOUT,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    /**
     * @brief Деструктор, автоматически освобождает блокировку
     */
    ~FileLockGuard();

    /**
     * @brief Проверяет, была ли блокировка успешно захвачена
     * @return true, если блокировка успешно захвачена
     */
    bool isLocked() const;

    /**
     * @brief Явно освобождает блокировку
     * @return true, если блокировка успешно освобождена
     */
    bool release();

    /**
     * @brief Пытается создать и захватить файл блокировки (статический метод)
     * @param lockFilePath Путь к файлу блокировки
     * @param mode Режим блокировки
     * @param waitStrategy Стратегия ожидания
     * @param timeout Таймаут ожидания в миллисекундах
     * @return true, если блокировка успешно захвачена
     */
    static bool
    acquireFileLock(const std::filesystem::path &lockFilePath, LockMode mode = LockMode::EXCLUSIVE,
                    LockWaitStrategy waitStrategy = LockWaitStrategy::TIMEOUT,
                    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    /**
     * @brief Освобождает ранее захваченную блокировку (статический метод)
     * @param lockFilePath Путь к файлу блокировки
     * @return true, если блокировка успешно освобождена
     */
    static bool releaseFileLock(const std::filesystem::path &lockFilePath);

private:
    // Запрещаем копирование и присваивание
    FileLockGuard(const FileLockGuard &) = delete;
    FileLockGuard &operator=(const FileLockGuard &) = delete;
    FileLockGuard(FileLockGuard &&) = delete;
    FileLockGuard &operator=(FileLockGuard &&) = delete;

    // Исходный путь к файлу, для которого реализуется блокировка
    const std::filesystem::path originalLockPath_;
    // Состояние блокировки
    bool locked_;
};
} // namespace octet::utils
