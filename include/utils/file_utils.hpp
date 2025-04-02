#pragma once

#include <string>
#include <optional>
#include <filesystem>

namespace octet::utils {
/**
 * @brief Проверяет существование директории и создает её (и все родительские директории) при
 * необходимости
 * @param dir Путь к директории
 * @param createIfMissing Создавать директорию, если отсутствует
 * @return true, если директория существует или была успешно создана
 */
bool ensureDirectoryExists(const std::filesystem::path &dir, bool createIfMissing = true);

/**
 * @brief Атомарно записывает данные в файл (через временный файл)
 * @param filePath Путь к файлу назначения
 * @param data Данные для записи
 * @return true, если запись выполнена успешно
 */
bool atomicFileWrite(const std::filesystem::path &filePath, const std::string &data);

/**
 * @brief Безопасно считывает все содержимое файла
 * @param filePath Путь к файлу для чтения
 * @param[out] data Буфер для сохранения прочитанных данных
 * @return true, если чтение выполнено успешно
 */
bool safeFileRead(const std::filesystem::path &filePath, std::string &data);

/**
 * @brief Проверяет, существует ли файл и доступен ли для чтения
 * @param filePath Путь к проверяемому файлу
 * @return true, если файл существует и доступен для чтения
 */
bool isFileReadable(const std::filesystem::path &filePath);

/**
 * @brief Безопасно добавляет данные в конец файла
 * @param filePath Путь к файлу для дополнения
 * @param data Данные для добавления
 * @return true, если добавление выполнено успешно
 */
bool safeFileAppend(const std::filesystem::path &filePath, const std::string &data);

/**
 * @brief Создаёт резервную копию файла с временной меткой в имени
 * @param filePath Путь к файлу для резервного копирования
 * @return Путь к созданной резервной копии или пустое значение в случае ошибки
 */
std::optional<std::filesystem::path> createFileBackup(const std::filesystem::path &filePath);

/**
 * @brief Пытается создать и захватить файл блокировки
 * @param lockFilePath Путь к файлу блокировки
 * @return true, если блокировка успешно захвачена
 */
bool acquireFileLock(const std::filesystem::path &lockFilePath);

/**
 * @brief Освобождает ранее захваченную блокировку
 * @param lockFilePath Путь к файлу блокировки
 * @return true, если блокировка успешно освобождена
 */
bool releaseFileLock(const std::filesystem::path &lockFilePath);
} // namespace octet::utils
