#include "utils/file_utils.hpp"

#include <cassert>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <random>
#include <thread>
#include <system_error>

#if defined(OCTET_PLATFORM_UNIX)
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#elif defined(OCTET_PLATFORM_WINDOWS)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#include "utils/compiler.hpp"
#include "utils/logger.hpp"
#include "utils/file_lock_guard.hpp"

namespace {
// Проверка существовании директории по указанному пути
bool isExistingDirectory(std::filesystem::path path)
{
    return std::filesystem::exists(path) && std::filesystem::is_directory(path);
}

// Получение текущего времени в виде строки для создания уникальных имен файлов
std::string getCurrentTimeFormatted()
{
    // Получаем текущее время из системных часов
    const auto now = std::chrono::system_clock::now();
    // now -> time_t для использования localtime
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    // Получаем миллисекунды текущей секунды
    const auto ms
        = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", std::localtime(&time_t_now));
    std::string result(buffer);
    result += "_" + std::to_string(ms.count());
    return result;
}

// Генерация случайного идентификатора для временных файлов
std::string generateRandomId(size_t length = 8)
{
    // Для каждого потока создаем свой экземпляр генератора
    thread_local std::mt19937 rng(std::random_device{}());
    static const char characters[]
        = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    // Обеспечиваем равномерное распределение целых чисел
    // `-2`, так как убираем `0` из массива + обеспечиваем индексацию от нулевого символа
    std::uniform_int_distribution<size_t> dist(0, sizeof(characters) - 2);

    std::string result(length, '\0');
    for (size_t i = 0; i < length; i++) {
        result[i] = characters[dist(rng)];
    }
    return result;
}

// Получение пути для временного файла (рядом с основным файлом)
std::filesystem::path getTempFilePath(const std::filesystem::path &originalPath)
{
    const auto parentPath = originalPath.parent_path();
    const auto filename = originalPath.filename().string();
    std::filesystem::path tempPath;
    do {
        // TODO: может стоит сделать файлы скрытыми (точка в начале названия файла)
        const auto tempFilename = filename + ".tmp." + generateRandomId();
        tempPath = parentPath / tempFilename;
    } while (std::filesystem::exists(tempPath));
    LOG_DEBUG << "Сгенерирован путь для временного файла: " << tempPath.string();
    return tempPath;
}

// Получение пути для резервной копии (рядом с основным файлом)
std::filesystem::path getBackupFilePath(const std::filesystem::path &originalPath)
{
    const auto parentPath = originalPath.parent_path();
    const auto filename = originalPath.filename().string();
    std::filesystem::path backupPath;
    do {
        // TODO: может стоит сделать файлы скрытыми (точка в начале названия файла)
        const auto backupFilename = filename + ".backup." + getCurrentTimeFormatted();
        backupPath = parentPath / backupFilename;
        // Добавляем небольшую задержку, если файл существует, что может возникнуть при работе в
        // многопоточной среде при условии, что не используются уникальные идентификаторы
        if (std::filesystem::exists(backupPath)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } while (std::filesystem::exists(backupPath));
    LOG_DEBUG << "Сгенерирован путь для резервной копии: " << backupPath.string();
    return backupPath;
}

// Синхронизация директории для безопасного сохранения метаданных
bool syncDirectory(const std::filesystem::path &dir)
{
    LOG_DEBUG << "Синхронизация директории: " << dir.string();
    // Работаем на более низком уровне, так как на высокоуровневом API не поддерживается прямая
    // работа с каталогами, тем более учитывая, что нужно сделать "нестандартную" операцию -
    // синхронизацию директории
#if defined(OCTET_PLATFORM_UNIX)
    const auto fd = open(dir.c_str(), O_RDONLY);
    if (fd == -1) {
        LOG_ERROR << "Не удалось открыть директорию для синхронизации: " << dir.string()
                  << ", ошибка: " << octet::utils::errnoToString(errno);
        return false;
    }

    const auto success = fsync(fd) == 0;
    close(fd);
    if (!success) {
        LOG_ERROR << "Ошибка синхронизации директории: " << dir.string()
                  << ", ошибка: " << octet::utils::errnoToString(errno);
    }
    return success;
#elif defined(OCTET_PLATFORM_WINDOWS)
    // Открываем дескриптор директории
    HANDLE hDir = CreateFileW(dir.wstring().c_str(), // Путь в формате wide string
                              GENERIC_READ, // Права на чтение
                              FILE_SHARE_READ | FILE_SHARE_WRITE
                                  | FILE_SHARE_DELETE, // Совместное использование
                              nullptr, // Атрибуты безопасности по умолчанию
                              OPEN_EXISTING, // Открытие только существующего каталога
                              FILE_FLAG_BACKUP_SEMANTICS, // Флаг для работы с каталогом
                              nullptr); // Шаблон не используем

    // Проверка, получилось ли открыть дескриптор
    if (hDir == INVALID_HANDLE_VALUE) {
        LOG_ERROR << "Не удалось открыть директорию для синхронизации: " << dir.string()
                  << ", ошибка: " << octet::utils::errnoToString(errno);
        return false;
    }

    // Сбрасываем буферы файловой системы для дескриптора каталога
    BOOL result = FlushFileBuffers(hDir);
    if (!result) {
        DWORD error = GetLastError();
        LOG_ERROR << "Ошибка синхронизации директории: " << dir.string()
                  << ", код ошибки: " << error;
    }
    CloseHandle(hDir);
    return result != 0;
#else
    UNREACHABLE("Unsupported platform");
#endif
}
} // namespace

namespace octet::utils {
bool ensureDirectoryExists(const std::filesystem::path &dir, bool createIfMissing)
{
    LOG_DEBUG << "Проверка директории: " << dir.string()
              << ", создавать если отсутствует: " << (createIfMissing ? "да" : "нет");

    std::error_code ec;
    if (std::filesystem::exists(dir, ec)) {
        assert(!ec);

        if (!std::filesystem::is_directory(dir, ec)) {
            if (ec) {
                LOG_ERROR << "Ошибка при проверке, является ли путь директорией: " << dir.string()
                          << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
                return false;
            }
            LOG_ERROR << "Путь существует, но не является директорией: " << dir.string();
            return false;
        }
        LOG_DEBUG << "Директория уже существует: " << dir.string();
        return true;
    }
    else if (ec) {
        LOG_ERROR << "Ошибка при проверке существования директории: " << dir.string()
                  << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
        return false;
    }

    if (!createIfMissing) {
        LOG_DEBUG << "Директория не существует и не будет создана: " << dir.string();
        return false;
    }

    // Создаем директорию и все родительские директории
    const auto created = std::filesystem::create_directories(dir, ec);
    if (ec) {
        LOG_ERROR << "Ошибка при создании директории: " << dir.string()
                  << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
        return false;
    }

    if (created) {
        LOG_INFO << "Создана директория: " << dir.string();
    }
    else {
        LOG_ERROR << "Не удалось создать директорию: " << dir.string();
    }
    return created;
}

bool atomicFileWrite(const std::filesystem::path &filePath, const std::string &data)
{
    LOG_DEBUG << "Атомарная запись в файл: " << filePath.string()
              << ", размер данных: " << data.size();
    if (isExistingDirectory(filePath)) {
        LOG_ERROR << "Ошибка при атомарной записи: " << filePath.string()
                  << " - это директория, а не файл";
        return false;
    }

    // Приобретаем эксклюзивную блокировку для файла
    FileLockGuard lock(filePath, LockMode::EXCLUSIVE);
    if (!lock.isLocked()) {
        LOG_ERROR << "Не удалось получить блокировку для файла: " << filePath.string();
        return false;
    }

    // Проверяем, существует ли родительская директория
    const auto parentDir = filePath.parent_path();
    if (!ensureDirectoryExists(parentDir)) {
        LOG_ERROR << "Не удалось обеспечить существование директории: " << parentDir.string();
        return false;
    }

    // Создаем временный файл в той же директории
    const auto tempFilePath = getTempFilePath(filePath);

    // Записываем данные во временный файл
    // Используем блок области видимости для гарантированного закрытия файла
    {
        std::ofstream outFile(tempFilePath, std::ios::binary);
        if (!outFile) {
            LOG_ERROR << "Не удалось открыть временный файл для записи: " << tempFilePath.string();
            return false;
        }

        outFile.write(data.data(), data.size());
        if (!outFile) {
            // Ошибка записи
            LOG_ERROR << "Не удалось записать данные во временный файл: " << tempFilePath.string();
            outFile.close();
            std::filesystem::remove(tempFilePath);
            return false;
        }

        outFile.flush();
        outFile.close();

        if (!outFile) {
            // Ошибка закрытия файла
            LOG_ERROR << "Не удалось закрыть временный файл: " << tempFilePath.string();
            std::filesystem::remove(tempFilePath);
            return false;
        }
    }

    std::error_code ec;
    // Пытаемся выполнить атомарное переименование без предварительного удаления
    LOG_DEBUG << "Переименование временного файла: " << tempFilePath.string() << " в "
              << filePath.string();
    std::filesystem::rename(tempFilePath, filePath, ec);

    // Если возникла ошибка при переименовании (некоторые ФС могут не поддерживать атомарную замену)
    if (ec) {
        // В этом случае удаляем существующий файл и пробуем снова
        if (std::filesystem::exists(filePath, ec)) {
            LOG_DEBUG << "Атомарное переименование не сработало. Удаление существующего файла "
                         "перед повторной попыткой: "
                      << filePath.string();

            // Создаем резервную копию существующего файла для безопасности
            const auto backupPath = createFileBackup(filePath);
            if (!backupPath.has_value()) {
                LOG_ERROR
                    << "Не удалось создать резервную копию исходного файла для атомарной записи: "
                    << filePath.string();
                return false;
            }

            // Удаляем существующий файл
            std::filesystem::remove(filePath, ec);
            if (ec) {
                assert(std::filesystem::exists(filePath));
                LOG_ERROR << "Не удалось удалить существующий файл: " << filePath.string()
                          << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
                std::filesystem::remove(tempFilePath);
                return false;
            }

            // Пробуем переименование снова
            std::filesystem::rename(tempFilePath, filePath, ec);
            if (ec) {
                LOG_ERROR << "Ошибка при повторном переименовании временного файла: "
                          << tempFilePath.string() << ", код ошибки: " << ec.value()
                          << ", сообщение: " << ec.message();

                // Пытаемся восстановить из резервной копии, если она есть
                if (std::filesystem::exists(*backupPath)) {
                    LOG_DEBUG << "Восстановление из резервной копии: " << (*backupPath).string();
                    std::filesystem::copy_file(*backupPath, filePath,
                                               std::filesystem::copy_options::overwrite_existing,
                                               ec);
                    if (ec) {
                        LOG_CRITICAL << "Не удалось восстановить из резервной копии: "
                                     << (*backupPath).string() << ", код ошибки: " << ec.value()
                                     << ", сообщение: " << ec.message();
                    }
                    else {
                        std::filesystem::remove(*backupPath);
                    }
                }
                else {
                    LOG_CRITICAL << "Не удалось восстановить из резервной копии: копия "
                                 << (*backupPath).string() << " была удалена";
                }
                std::filesystem::remove(tempFilePath);
                return false;
            }
        }
        else {
            if (ec) {
                LOG_ERROR << "Ошибка при переименовании временного файла: " << tempFilePath.string()
                          << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
            }
            else {
                LOG_ERROR << "Не удалось переименовать временный файл: " << tempFilePath.string();
            }

            std::filesystem::remove(tempFilePath);
            return false;
        }
    }

    if (!syncDirectory(parentDir)) {
        LOG_WARNING << "Файл записан, но синхронизация директории не удалась: "
                    << filePath.string();
        return false;
    }

    LOG_INFO << "Атомарная запись успешно завершена: " << filePath.string()
             << ", размер: " << data.size();
    return true;
}

bool safeFileRead(const std::filesystem::path &filePath, std::string &data)
{
    LOG_DEBUG << "Безопасное чтение файла: " << filePath.string();

    if (isExistingDirectory(filePath)) {
        LOG_ERROR << "Ошибка при безопасном чтении: " << filePath.string()
                  << " - это директория, а не файл";
        return false;
    }

    // Используем разделяемую блокировку для чтения
    FileLockGuard lock(filePath, LockMode::SHARED);
    if (!lock.isLocked()) {
        LOG_ERROR << "Не удалось получить блокировку для чтения файла: " << filePath.string();
        return false;
    }

    if (!isFileReadable(filePath)) {
        LOG_ERROR << "Файл не доступен для чтения: " << filePath.string();
        return false;
    }

    std::ifstream inFile(filePath, std::ios::binary | std::ios::ate);
    if (!inFile) {
        LOG_ERROR << "Не удалось открыть файл для чтения: " << filePath.string();
        return false;
    }

    // Определяем размер файла и выделяем буфер
    const auto fileSize = inFile.tellg();
    if (fileSize < 0) {
        LOG_ERROR << "Ошибка при определении размера файла: " << filePath.string();
        return false;
    }

    LOG_DEBUG << "Чтение файла размером " << fileSize << " байт: " << filePath.string();
    data.resize(static_cast<size_t>(fileSize));
    // Перемещаемся в начало файла и читаем содержимое
    inFile.seekg(0);
    inFile.read(&data[0], fileSize);

    const auto readSize = inFile.tellg();
    if (static_cast<size_t>(readSize) != static_cast<size_t>(fileSize)) {
        LOG_ERROR << "Ошибка при чтении: " << filePath.string() << ", прочитано " << inFile.gcount()
                  << " байт из " << fileSize << " ожидаемых";
        return false;
    }

    LOG_DEBUG << "Успешно прочитано " << readSize << " байт: " << filePath.string();
    return true;
}

bool isFileReadable(const std::filesystem::path &filePath)
{
    LOG_DEBUG << "Проверка файла на чтение: " << filePath.string();

    // Проверка существования файла
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) {
        if (ec) {
            LOG_ERROR << "Ошибка при проверке существования файла: " << filePath.string()
                      << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
        }
        else {
            LOG_DEBUG << "Файл не существует: " << filePath.string();
        }
        return false;
    }

    if (std::filesystem::is_directory(filePath)) {
        LOG_DEBUG << "По указанному пути находится директория, а не файл: " << filePath.string();
        return false;
    }

    // Пытаемся открыть файл для чтения, чтобы убедиться в доступности
    std::ifstream testFile(filePath);
    const auto readable = testFile.good();
    if (readable) {
        LOG_DEBUG << "Файл доступен для чтения: " << filePath.string();
    }
    else {
        LOG_DEBUG << "Файл существует, но недоступен для чтения: " << filePath.string();
    }
    return readable;
}

bool safeFileAppend(const std::filesystem::path &filePath, const std::string &data)
{
    LOG_DEBUG << "Безопасное добавление данных в файл: " << filePath.string()
              << ", размер данных: " << data.size();
    if (isExistingDirectory(filePath)) {
        LOG_ERROR << "Ошибка при безопасном добавлении данных: " << filePath.string()
                  << " - это директория, а не файл";
        return false;
    }

    // Используем эксклюзивную блокировку для добавления
    FileLockGuard lock(filePath, LockMode::EXCLUSIVE);
    if (!lock.isLocked()) {
        LOG_ERROR << "Не удалось получить блокировку для добавления в файл: " << filePath.string();
        return false;
    }

    // Проверяем, существует ли родительская директория
    const auto parentDir = filePath.parent_path();
    if (!ensureDirectoryExists(parentDir)) {
        LOG_ERROR << "Не удалось обеспечить существование директории: " << parentDir.string();
        return false;
    }

    // Если файл не существует, создаем его с нуля
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) {
        if (ec) {
            LOG_ERROR << "Ошибка при проверке существования файла: " << filePath.string()
                      << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
            return false;
        }

        LOG_WARNING << "Невозможно добавить данные в несуществующий файл: " << filePath.string()
                    << ", пробуем выполнить атомарную запись данных";
        // Освобождаем блокировку для избежания deadlock
        lock.release();
        return atomicFileWrite(filePath, data);
    }

    // Открываем файл для добавления
    std::ofstream outFile(filePath, std::ios::binary | std::ios::app);
    if (!outFile) {
        LOG_ERROR << "Не удалось открыть файл для записи: " << filePath.string();
        return false;
    }

    const auto dataSize = data.size();
    // Записываем данные
    outFile.write(data.data(), dataSize);
    if (!outFile) {
        LOG_ERROR << "Ошибка при добавлении данных в файл: " << filePath.string();
        return false;
    }

    // Сбрасываем данные на диск
    outFile.flush();

    if (!outFile.good()) {
        LOG_ERROR << "Ошибка при закрытии файла после добавления: " << filePath.string();
        return false;
    }

    if (!syncDirectory(parentDir)) {
        LOG_WARNING << "Файл обновлен, но синхронизация директории не удалась: "
                    << filePath.string();
        return false;
    }

    LOG_DEBUG << "Успешно добавлено " << dataSize << " байт к файлу: " << filePath.string();
    return true;
}

std::optional<std::filesystem::path> createFileBackup(const std::filesystem::path &filePath)
{
    LOG_DEBUG << "Создание резервной копии файла: " << filePath.string();

    if (isExistingDirectory(filePath)) {
        LOG_ERROR << "Ошибка при создании резервной копии: " << filePath.string()
                  << " - это директория, а не файл";
        return std::nullopt;
    }

    // Используем разделяемую блокировку для чтения исходного файла
    FileLockGuard lock(filePath, LockMode::SHARED);
    if (!lock.isLocked()) {
        LOG_ERROR << "Не удалось получить блокировку для создания резервной копии: "
                  << filePath.string();
        return std::nullopt;
    }

    if (!isFileReadable(filePath)) {
        LOG_ERROR << "Не удается создать резервную копию: файл не доступен для чтения: "
                  << filePath.string();
        return std::nullopt;
    }

    // Получаем путь для резервной копии
    const auto backupPath = getBackupFilePath(filePath);
    LOG_INFO << "Создание резервной копии: " << filePath.string() << " -> " << backupPath.string();

    // Копируем файл в резервную копию
    std::error_code ec;
    std::filesystem::copy_file(filePath, backupPath,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        LOG_ERROR << "Ошибка при создании резервной копии: " << backupPath.string()
                  << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
        return std::nullopt;
    }

    if (!syncDirectory(filePath.parent_path())) {
        LOG_WARNING << "Резервная копия создана, но синхронизация директории не удалась: "
                    << filePath.string();
        return std::nullopt;
    }

    LOG_INFO << "Успешно создана резервная копия: " << backupPath.string();
    return backupPath;
}
} // namespace octet::utils
