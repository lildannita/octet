#include "utils/file_utils.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>
#include <mutex>
#include <random>
#include <thread>
#include <system_error>
#include <unordered_map>

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

namespace {
// Глобальные контейнеры для хранения активных дескрипторов по lock-файлам
#if defined(OCTET_PLATFORM_UNIX)
static std::unordered_map<std::string, int> fileLockMap;
#elif defined(OCTET_PLATFORM_WINDOWS)
static std::unordered_map<std::string, HANDLE> fileLockMap;
#endif
// Мьютекс для безопасного доступа к контейнерам из нескольких потоков
static std::mutex fileLockMutex;

// Получение текущего времени в виде строки для создания уникальных имен файлов
std::string getCurrentTimeFormatted()
{
    // Получаем текущее время из системных часов
    auto now = std::chrono::system_clock::now();
    // now -> time_t для использования localtime
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    // Получаем миллисекунды текущей секунды
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

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

// Синхронизация директории для обеспечения сохранности файловых операций
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
                  << ", ошибка: " << strerror(errno);
        return false;
    }

    const auto result = fsync(fd);
    close(fd);
    if (result != 0) {
        LOG_ERROR << "Ошибка синхронизации директории: " << dir.string()
                  << ", ошибка: " << strerror(errno);
    }
    return result == 0;
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
                  << ", ошибка: " << strerror(errno);
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

// Получение PID текущего процесса в виде строки
std::string getCurrentPid()
{
#if defined(OCTET_PLATFORM_UNIX)
    return std::to_string(getpid());
#elif defined(OCTET_PLATFORM_WINDOWS)
    return std::to_string(GetCurrentProcessId());
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
        if (ec) {
            LOG_ERROR << "Ошибка при проверке существования директории: " << dir.string()
                      << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
            return false;
        }

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
    bool created = std::filesystem::create_directories(dir, ec);
    if (ec) {
        LOG_ERROR << "Ошибка при создании директории: " << dir.string()
                  << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
        return false;
    }

    if (!created) {
        LOG_ERROR << "Не удалось создать директорию: " << dir.string();
    }

    LOG_INFO << "Создана директория: " << dir.string();
    return created;
}

bool atomicFileWrite(const std::filesystem::path &filePath, const std::string &data)
{
    LOG_DEBUG << "Атомарная запись в файл: " << filePath.string()
              << ", размер данных: " << data.size();

    // Проверяем, существует ли родительская директория
    auto parentDir = filePath.parent_path();
    if (!ensureDirectoryExists(parentDir)) {
        LOG_ERROR << "Не удалось обеспечить существование директории: " << parentDir.string();
        return false;
    }

    // Создаем временный файл в той же директории
    auto tempFilePath = getTempFilePath(filePath);

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
                        LOG_ERROR << "Не удалось восстановить из резервной копии: "
                                  << (*backupPath).string() << ", код ошибки: " << ec.value()
                                  << ", сообщение: " << ec.message();
                    }
                    else {
                        std::filesystem::remove(*backupPath);
                    }
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

    syncDirectory(parentDir);
    LOG_INFO << "Атомарная запись успешно завершена: " << filePath.string()
             << ", размер: " << data.size();
    return true;
}

bool safeFileRead(const std::filesystem::path &filePath, std::string &data)
{
    LOG_DEBUG << "Безопасное чтение файла: " << filePath.string();

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
    bool readable = testFile.good();
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

    // Проверяем, существует ли родительская директория
    auto parentDir = filePath.parent_path();
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

    LOG_DEBUG << "Успешно добавлено " << dataSize << " байт к файлу: " << filePath.string();
    return true;
}

std::optional<std::filesystem::path> createFileBackup(const std::filesystem::path &filePath)
{
    LOG_DEBUG << "Создание резервной копии файла: " << filePath.string();

    if (!isFileReadable(filePath)) {
        LOG_ERROR << "Не удается создать резервную копию: файл не доступен для чтения: "
                  << filePath.string();
        return std::nullopt;
    }

    // Получаем путь для резервной копии
    auto backupPath = getBackupFilePath(filePath);
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

    LOG_INFO << "Успешно создана резервная копия: " << backupPath.string();
    return backupPath;
}

bool acquireFileLock(const std::filesystem::path &lockFilePath)
{
    LOG_DEBUG << "Попытка получения блокировки: " << lockFilePath.string();

    // Проверяем, существует ли родительская директория
    auto parentDir = lockFilePath.parent_path();
    if (!ensureDirectoryExists(parentDir)) {
        LOG_ERROR << "Не удалось обеспечить существование директории для файла блокировки: "
                  << parentDir.string();
        return false;
    }

#if defined(OCTET_PLATFORM_SUPPORTED)
    // Преобразуем путь к lock-файлу в строку для использования в глобальном контейнере
    const std::string lockPathStr = lockFilePath.string();

    // Блокируем мьютекс для защиты доступа к глобальному контейнеру
    std::lock_guard<std::mutex> guard(fileLockMutex);

    // Если блокировка для данного файла уже захвачена, возвращаем false
    if (fileLockMap.find(lockPathStr) != fileLockMap.end()) {
        LOG_WARNING << "Блокировка для файла уже захвачена в текущем процессе: "
                    << lockFilePath.string();

        return false;
    }
#else
    UNREACHABLE("Unsupported platform");
#endif

#if defined(OCTET_PLATFORM_UNIX)
    // Открываем (или создаём, если не существует) файл блокировки с правами чтения/записи
    const auto fd = open(lockFilePath.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd == -1) {
        LOG_ERROR << "Не удалось открыть файл блокировки: " << lockFilePath.string()
                  << ", ошибка: " << strerror(errno);
        return false;
    }

    // Пытаемся получить эксклюзивную блокировку без ожидания
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        LOG_ERROR << "Не удалось получить блокировку: " << lockFilePath.string()
                  << ", ошибка: " << strerror(errno);
        close(fd);
        return false;
    }

    // Записываем PID текущего процесса в файл блокировки (для отладки и информативности)
    const auto pid = getCurrentPid();
    std::string pidStr = pid + "\n";
    const auto writeResult = write(fd, pidStr.c_str(), pidStr.size());
    if (writeResult == -1) {
        LOG_WARNING << "Не удалось записать PID в файл блокировки: " << lockFilePath.string()
                    << ", ошибка: " << strerror(errno);
    }

    // Сохраняем файловый дескриптор в глобальном контейнере по ключу lockPathStr
    fileLockMap[lockPathStr] = fd;
    LOG_INFO << "Успешно получена блокировка: " << lockFilePath.string() << ", PID: " << pid;
    return true;
#elif defined(OCTET_PLATFORM_WINDOWS)
    // Открываем дескриптор файла
    HANDLE fileHandle = CreateFileW(lockFilePath.wstring().c_str(), // Путь к формате wide string
                                    GENERIC_READ | GENERIC_WRITE, // Права на чтение и запись
                                    0, // Запрещаем совместное использование
                                    nullptr, // Атрибуты безопасности по умолчанию
                                    OPEN_ALWAYS, // Открыть существующий или создать новый
                                    FILE_ATTRIBUTE_NORMAL, // Обычные атрибуты файла
                                    nullptr // Шаблон не используем
    );

    // Проверка, получилось ли открыть дескриптор
    if (fileHandle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        LOG_ERROR << "Не удалось открыть файл блокировки: " << lockFilePath.string()
                  << ", ошибка: " << error;
        return false;
    }

    // Записываем PID в файл блокировки
    const auto pid = getCurrentPid();
    std::string pidStr = getCurrentPid() + "\n";
    DWORD bytesWritten = 0;
    const auto writeResult = WriteFile(fileHandle, pidStr.c_str(),
                                       static_cast<DWORD>(pidStr.size()), &bytesWritten, nullptr);
    if (!writeResult) {
        DWORD error = GetLastError();
        LOG_WARNING << "Не удалось записать PID в файл блокировки: " << lockFilePath.string()
                    << ", ошибка: " << error;
    }

    // Сохраняем дескриптор файла блокировки в глобальном контейнере
    fileLockMap[lockPathStr] = fileHandle;
    LOG_INFO << "Успешно получена блокировка: " << lockFilePath.string() << ", PID: " << pid;
    return true;
#else
    UNREACHABLE("Unsupported platform");
#endif
}

bool releaseFileLock(const std::filesystem::path &lockFilePath)
{
    LOG_DEBUG << "Освобождение блокировки: " << lockFilePath.string();

#if defined(OCTET_PLATFORM_SUPPORTED)
    const std::string lockPathStr = lockFilePath.string();
    // Блокируем мьютекс для защиты глобального контейнера
    std::lock_guard<std::mutex> guard(fileLockMutex);
    // Ищем дескриптор для данного lock-файла
    auto it = fileLockMap.find(lockPathStr);
    if (it == fileLockMap.end()) {
        LOG_WARNING << "Попытка освободить несуществующую блокировку: " << lockFilePath.string();
        return false;
    }
#else
    UNREACHABLE("Unsupported platform");
#endif

#if defined(OCTET_PLATFORM_UNIX)
    // Получаем дескриптор для данного lock-файла
    const auto fd = it->second;
    // Снимаем блокировку
    if (flock(fd, LOCK_UN) != 0) {
        LOG_ERROR << "Ошибка при снятии блокировки: " << lockFilePath.string()
                  << ", ошибка: " << strerror(errno);
    }
    // Закрываем файловый дескриптор
    if (close(fd) != 0) {
        LOG_ERROR << "Ошибка при закрытии файлового дескриптора: " << lockFilePath.string()
                  << ", ошибка: " << strerror(errno);
    }

    // Удаляем файл блокировки
    if (unlink(lockFilePath.c_str()) != 0) {
        LOG_ERROR << "Не удалось удалить файл блокировки: " << lockFilePath.string()
                  << ", ошибка: " << strerror(errno);
        return false;
    }
    // Удаляем запись из контейнера
    fileLockMap.erase(it);

    LOG_INFO << "Блокировка успешно освобождена: " << lockFilePath.string();
    return true;
#elif defined(OCTET_PLATFORM_WINDOWS)
    // Получаем дескриптор для данного lock-файла
    HANDLE fileHandle = it->second;
    // Закрываем дескриптор файла
    if (!CloseHandle(fileHandle)) {
        DWORD error = GetLastError();
        LOG_ERROR << "Ошибка при закрытии дескриптора файла: " << lockFilePath.string()
                  << ", ошибка: " << error;
    }

    // Удаляем файл блокировки
    if (!DeleteFileW(lockFilePath.wstring().c_str())) {
        DWORD error = GetLastError();
        LOG_ERROR << "Не удалось удалить файл блокировки: " << lockFilePath.string()
                  << ", код ошибки Windows: " << error;
        return false;
    }
    // Удаляем запись из контейнера
    fileLockMap.erase(it);

    LOG_INFO << "Блокировка успешно освобождена: " << lockFilePath.string();
    return true;
#else
    UNREACHABLE("Unsupported platform");
#endif
}
} // namespace octet::utils
