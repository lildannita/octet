#include "utils/file_lock_guard.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(OCTET_PLATFORM_UNIX)
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef int FileDescriptor;
#elif defined(OCTET_PLATFORM_WINDOWS)
#include <fcntl.h>
#include <io.h>
#include <windows.h>

typedef HANDLE FileDescriptor;
#endif

#include "utils/compiler.hpp"
#include "logger.hpp"

namespace {
// Структура для хранения информации о блокировке
struct LockInfo {
public:
    LockInfo(FileDescriptor fd, octet::utils::LockMode mode, std::thread::id threadId)
        : fd_(fd)
        , mode_(mode)
        , threadIds_({ threadId })
        , refCount_(1)
    {
    }
    // Геттер для дескриптора
    FileDescriptor getFileDescriptor() const
    {
        return fd_;
    }
    // Геттер для режима блокировки
    octet::utils::LockMode getMode() const
    {
        return mode_;
    }
    // Проверка, захвачена ли блокировка вызывающим потоком
    bool lockedByThread(std::thread::id threadId) const
    {
        return std::find(threadIds_.begin(), threadIds_.end(), threadId) != threadIds_.end();
    }
    // Геттер для счетчика ссылок
    size_t getRefCount() const
    {
        return refCount_;
    }
    // Увеличение счетчика ссылок на единицу
    void incrementRefCount(std::thread::id threadId)
    {
        if (mode_ == octet::utils::LockMode::SHARED) {
            refCount_++;
            threadIds_.push_back(threadId);
        }
        // LCOV_EXCL_START
        else {
            UNREACHABLE("Increment function must be called only for SHARED mode");
        }
        // LCOV_EXCL_STOP
    }
    // Уменьшение счетчика ссылок на единицу
    void decrementRefCount(std::thread::id threadId)
    {
        // LCOV_EXCL_START
        if (refCount_ == 0) {
            UNREACHABLE("Trying to decrease empty reference counter for shared locks");
        }
        // LCOV_EXCL_STOP

        if (mode_ == octet::utils::LockMode::SHARED) {
            refCount_--;
            // Удаляем первое вхождение идентификатора потока
            auto it = std::find(threadIds_.begin(), threadIds_.end(), threadId);
            // LCOV_EXCL_START
            if (it == threadIds_.end()) {
                UNREACHABLE("Trying to decrease reference count from unauthorized thread");
            }
            // LCOV_EXCL_STOP
            threadIds_.erase(it);
        }
        // LCOV_EXCL_START
        else {
            UNREACHABLE("Decrement function must be called only for SHARED mode");
        }
        // LCOV_EXCL_STOP
    }

private:
    const FileDescriptor fd_; // Дескриптор lock-файла
    const octet::utils::LockMode mode_; // Режим блокировки
    std::vector<std::thread::id> threadIds_; // Идентификаторы потоков, захвативших блокировку
    size_t refCount_; // Счетчик ссылок для разделяемых блокировок
};

// Глобальный контейнер для хранения информации о блокировках
static std::unordered_map<std::string, LockInfo> fileLockMap;
// Мьютекс для безопасного доступа к контейнерам из нескольких потоков
static std::mutex fileLockMutex;

/**
 * @brief Получение PID текущего процесса
 * @return Строка с PID текущего процесса
 */
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

std::string getLockModeString(octet::utils::LockMode mode)
{
    switch (mode) {
    case octet::utils::LockMode::EXCLUSIVE:
        return "EXCLUSIVE";
    case octet::utils::LockMode::SHARED:
        return "SHARED";
    }
    UNREACHABLE("Unsupported LockMode"); // LCOV_EXCL_LINE
}

/**
 * @brief Приостанавливает блокировку мьютекса
 * @param lock Обертка над мьютексом с блокировкой
 * @param pause Время паузы (в миллисекундах)
 */
void pauseLock(std::unique_lock<std::mutex> &lock, size_t pause = 5)
{
    lock.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(pause));
    lock.lock();
}

/**
 * @brief Формирует путь к файлу блокировки по пути к исходному файлу
 * @param filePath Путь к исходному файлу
 * @return Путь к файлу блокировки
 */
std::filesystem::path getLockFilePath(const std::filesystem::path &filePath)
{
    return std::filesystem::path(filePath.string() + ".lock");
}

bool checkDirectoryExists(const std::filesystem::path &dir)
{
    LOG_DEBUG << "Проверка директории для файла блокировки: " << dir.string();

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
} // namespace

namespace octet::utils {
FileLockGuard::FileLockGuard(const std::filesystem::path &filePath, LockMode mode,
                             LockWaitStrategy waitStrategy, std::chrono::milliseconds timeout)
    : originalLockPath_(filePath)
    , locked_(acquireFileLock(originalLockPath_, mode, waitStrategy, timeout))
{
}

FileLockGuard::~FileLockGuard()
{
    if (locked_) {
        release();
    }
}

bool FileLockGuard::isLocked() const
{
    return locked_;
}

bool FileLockGuard::release()
{
    if (!locked_) {
        return false;
    }

    bool result = releaseFileLock(originalLockPath_);
    if (result) {
        locked_ = false;
    }
    return result;
}

bool FileLockGuard::acquireFileLock(const std::filesystem::path &filePath, LockMode mode,
                                    LockWaitStrategy waitStrategy,
                                    std::chrono::milliseconds timeout)
{
    LOG_DEBUG << "Попытка получения блокировки: " << filePath.string()
              << ", режим: " << (mode == LockMode::EXCLUSIVE ? "эксклюзивный" : "разделяемый");

    // Проверяем, существует ли родительская директория
    auto parentDir = filePath.parent_path();
    if (!checkDirectoryExists(parentDir)) {
        LOG_ERROR << "Не удалось обеспечить существование директории для файла блокировки: "
                  << parentDir.string();
        return false;
    }

#if defined(OCTET_PLATFORM_SUPPORTED)
    // Получаем путь к lock-файлу
    const auto lockPath = getLockFilePath(filePath);
    const auto lockPathStr = lockPath.string();

    // Текущий ID потока
    const auto currentThreadId = std::this_thread::get_id();

    // Блокируем мьютекс для защиты доступа к глобальному контейнеру
    std::unique_lock<std::mutex> lock(fileLockMutex);

    // Проверяем, захвачена ли уже блокировка текущим процессом
    auto it = fileLockMap.find(lockPathStr);
    if (it != fileLockMap.end()) {
        // Проверяем совместимость режимов блокировки
        const auto bothShared
            = (mode == LockMode::SHARED && it->second.getMode() == LockMode::SHARED);

        // Если оба режима разделяемые, увеличиваем счетчик ссылок
        if (bothShared) {
            const auto refCount = it->second.getRefCount();
            it->second.incrementRefCount(currentThreadId);
            const auto newRefCount = it->second.getRefCount();
            LOG_DEBUG << "Увеличен счетчик ссылок для разделяемой блокировки: " << filePath.string()
                      << ", изменение значения: " << refCount << " -> " << newRefCount;
            return true;
        }

        // Если блокировка принадлежит текущему потоку - это самоблокировка
        if (it->second.lockedByThread(currentThreadId)) {
            LOG_ERROR << "Попытка повторного захвата блокировки в том же потоке: "
                      << filePath.string() << ". Это может привести к deadlock!";
            return false;
        }

        // Если режимы несовместимы и стратегия без ожидания, сразу выходим
        if (!bothShared && waitStrategy == LockWaitStrategy::INSTANTLY) {
            LOG_WARNING << "Блокировка для файла уже захвачена другим потоком: "
                        << filePath.string();
            return false;
        }

        // Для других стратегий ожидания, ждем освобождения блокировки
        auto startTime = std::chrono::steady_clock::now();
        bool timeoutExceeded = false;
        do {
            // Временно освобождаем мьютекс, чтобы другие потоки могли освободить блокировку
            pauseLock(lock);

            // Проверяем, не освободилась ли блокировка
            it = fileLockMap.find(lockPathStr);
            if (it == fileLockMap.end()) {
                // Блокировка освободилась, можно продолжить
                break;
            }

            // Проверяем таймаут для стратегии с ограниченным ожиданием
            if (waitStrategy == LockWaitStrategy::TIMEOUT) {
                const auto currentTime = std::chrono::steady_clock::now();
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - startTime);
                timeoutExceeded = (elapsed >= timeout);
            }
        } while (!timeoutExceeded && waitStrategy != LockWaitStrategy::INSTANTLY);

        // Если блокировка всё ещё захвачена и истёк таймаут или нет ожидания
        if (fileLockMap.find(lockPathStr) != fileLockMap.end()
            && (timeoutExceeded || waitStrategy == LockWaitStrategy::INSTANTLY)) {
            LOG_WARNING << "Таймаут ожидания освобождения блокировки в текущем процессе: "
                        << filePath.string();
            return false;
        }
    }
#else
    UNREACHABLE("Unsupported platform");
#endif

#if defined(OCTET_PLATFORM_UNIX)
    // Открываем (или создаём) файл блокировки с правами чтения/записи для всех
    const auto fd = open(lockPath.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd == -1) {
        LOG_ERROR << "Не удалось открыть файл блокировки: " << filePath.string()
                  << ", ошибка: " << errnoToString(errno);
        return false;
    }

    // Определяем тип блокировки в зависимости от режима
    int lockType = -1;
    switch (mode) {
    case LockMode::EXCLUSIVE:
        lockType = LOCK_EX; // Exclusive lock
        break;
    case LockMode::SHARED:
        lockType = LOCK_SH; // Shared lock
        break;
    // LCOV_EXCL_START
    default:
        UNREACHABLE("Unsupported LockMode");
        // LCOV_EXCL_STOP
    }

    switch (waitStrategy) {
    // Стандартная стратегия (бесконечное ожидание)
    case LockWaitStrategy::STANDARD: {
        if (flock(fd, lockType) != 0) {
            LOG_ERROR << "Не удалось получить блокировку с бесконечным ожиданием: "
                      << filePath.string() << ", ошибка: " << errnoToString(errno);
            close(fd);
            return false;
        }
        break;
    }
    // Стратегия без ожидания
    case LockWaitStrategy::INSTANTLY: {
        lockType |= LOCK_NB; // Non-blocking
        if (flock(fd, lockType) != 0) {
            LOG_ERROR << "Не удалось получить блокировку без ожидания: " << filePath.string()
                      << ", ошибка: " << errnoToString(errno);
            close(fd);
            return false;
        }
        break;
    }
    // Стратегия с таймаутом ожидания
    case LockWaitStrategy::TIMEOUT: {
        const auto startTime = std::chrono::steady_clock::now();
        while (true) {
            // Пытаемся получить блокировку без ожидания
            if (flock(fd, lockType | LOCK_NB) == 0) {
                break;
            }

            // Если ошибка не EWOULDBLOCK, значит, что-то пошло не так
            if (errno != EWOULDBLOCK) {
                LOG_ERROR << "Не удалось получить блокировку: " << filePath.string()
                          << ", ошибка: " << errnoToString(errno);
                close(fd);
                return false;
            }

            const auto currentTime = std::chrono::steady_clock::now();
            const auto elapsed
                = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime);

            if (elapsed >= timeout) {
                LOG_WARNING << "Таймаут ожидания блокировки: " << filePath.string();
                close(fd);
                return false;
            }

            // Временно освобождаем мьютекс, чтобы другие потоки могли освободить блокировку
            pauseLock(lock);
        }
        break;
    }
    // LCOV_EXCL_START
    default:
        UNREACHABLE("Unsupported LockWaitStrategy");
        // LCOV_EXCL_STOP
    }

    // Записываем PID текущего процесса и режим блокировки в файл (для отладки и информативности)
    const std::string lockInfo = "PID: " + getCurrentPid() + " ThreadID: "
                                 + std::to_string(std::hash<std::thread::id>{}(currentThreadId))
                                 + " Mode: " + getLockModeString(mode) + "\n";

    // Усекаем файл и пишем в начало
    ftruncate(fd, 0);
    lseek(fd, 0, SEEK_SET);

    // Записываем информацию в файл
    const auto writeResult = write(fd, lockInfo.c_str(), lockInfo.size());
    if (writeResult == -1) {
        // Запись в lock-файле не так важна, поэтому просто кидаем предупреждение
        LOG_WARNING << "Не удалось записать информацию в файл блокировки: " << filePath.string()
                    << ", ошибка: " << errnoToString(errno);
    }
#elif defined(OCTET_PLATFORM_WINDOWS)
    // Для Windows настраиваем режим доступа в зависимости от типа блокировки
    DWORD winMode;
    switch (mode) {
    case LockMode::EXCLUSIVE:
        // Для эксклюзивной блокировки запрещаем любой совместный доступ
        winMode = 0;
        break;
    case LockMode::SHARED:
        winMode = FILE_SHARE_READ;
        break;
    // LCOV_EXCL_START
    default:
        UNREACHABLE("Unsupported LockMode");
        // LCOV_EXCL_STOP
    }

    auto openHandle = [&lockPath, &winMode]() {
        return CreateFileW(lockPath.wstring().c_str(), // Путь в формате wide string
                           GENERIC_READ | GENERIC_WRITE, // Права на чтение и запись
                           winMode, // Режим совместного использования
                           nullptr, // Атрибуты безопасности по умолчанию
                           OPEN_ALWAYS, // Открыть существующий или создать новый
                           FILE_ATTRIBUTE_NORMAL, // Обычные атрибуты файла
                           nullptr // Шаблон не используем
        );
    };

    // Получаем дескриптор файла
    auto fd = openHandle();
    // Проверка на получение дескриптора с первого раза
    if (fd == INVALID_HANDLE_VALUE) {
        switch (waitStrategy) {
        // Стратегия без ожидания
        case LockWaitStrategy::INSTANTLY: {
            LOG_ERROR << "Не удалось открыть файл блокировки: " << filePath.string()
                      << ", ошибка: " << GetLastError();
            return false;
        }
        // Стандартная стратегия (бесконечное ожидание)
        case LockWaitStrategy::STANDARD: {
            while (true) {
                // Временно освобождаем мьютекс, чтобы другие потоки могли освободить блокировку
                pauseLock(lock);
                // Пытаемся снова получить дескрипттор
                fd = openHandle();
                if (fd != INVALID_HANDLE_VALUE) {
                    break;
                }
            }
            break;
        }
        // Стратегия с таймаутом ожидания
        case LockWaitStrategy::TIMEOUT: {
            const auto startTime = std::chrono::steady_clock::now();
            while (true) {
                // Временно освобождаем мьютекс, чтобы другие потоки могли освободить блокировку
                pauseLock(lock);
                // Пытаемся снова получить дескрипттор
                fd = openHandle();
                if (fd != INVALID_HANDLE_VALUE) {
                    break;
                }

                const auto currentTime = std::chrono::steady_clock::now();
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - startTime);
                if (elapsed >= timeout) {
                    LOG_WARNING << "Таймаут ожидания блокировки: " << filePath.string();
                    return false;
                }
            }
            break;
        }
        // LCOV_EXCL_START
        default:
            UNREACHABLE("Unsupported LockWaitStrategy");
            // LCOV_EXCL_STOP
        }
    }

    // Записываем PID, ThreadID и режим блокировки в файл
    const std::string lockInfo = "PID: " + getCurrentPid() + " ThreadID: "
                                 + std::to_string(std::hash<std::thread::id>{}(currentThreadId))
                                 + " Mode: " + getLockModeString(mode) + "\n";

    // Усекаем файл и пишем в начало
    SetFilePointer(fd, 0, NULL, FILE_BEGIN);
    SetEndOfFile(fd);

    DWORD bytesWritten = 0;
    const auto writeResult = WriteFile(fd, lockInfo.c_str(), static_cast<DWORD>(lockInfo.size()),
                                       &bytesWritten, nullptr);
    if (!writeResult) {
        // Запись в lock-файле не так важна, поэтому просто кидаем предупреждение
        LOG_WARNING << "Не удалось записать информацию в файл блокировки: " << lockFilePath.string()
                    << ", ошибка: " << GetLastError();
    }
#else
    UNREACHABLE("Unsupported platform");
#endif

#if defined(OCTET_PLATFORM_SUPPORTED)
    // Сохраняем информацию о блокировке в глобальном контейнере
    fileLockMap.emplace(lockPathStr, LockInfo(fd, mode, currentThreadId));

    LOG_INFO << "Успешно получена блокировка: " << filePath.string() << " (" << lockPathStr
             << "), режим: " << getLockModeString(mode);
    return true;
#else
    UNREACHABLE("Unsupported platform");
#endif
}

bool FileLockGuard::releaseFileLock(const std::filesystem::path &filePath)
{
    LOG_DEBUG << "Освобождение блокировки: " << filePath.string();

#if defined(OCTET_PLATFORM_SUPPORTED)
    // Получаем путь к lock-файлу
    const auto lockPath = getLockFilePath(filePath);
    const auto lockPathStr = lockPath.string();

    // Блокируем мьютекс для защиты глобального контейнера
    std::lock_guard<std::mutex> guard(fileLockMutex);

    // Ищем информацию о блокировке для данного lock-файла
    auto it = fileLockMap.find(lockPathStr);
    if (it == fileLockMap.end()) {
        LOG_WARNING << "Попытка освободить несуществующую блокировку: " << lockPathStr;
        return false;
    }

    // Получаем информацию о блокировке
    auto &info = it->second;

    // Проверяем, что блокировка освобождается тем же потоком, который ее захватил
    const auto currentThreadId = std::this_thread::get_id();
    if (!info.lockedByThread(currentThreadId)) {
        LOG_ERROR << "Попытка освободить блокировку из неидентифицированного потока: "
                  << lockPathStr;
        return false;
    }

    // Уменьшаем счетчик ссылок для разделяемых блокировок
    const auto refCount = info.getRefCount();
    if (info.getMode() == LockMode::SHARED && refCount > 1) {
        info.decrementRefCount(currentThreadId);
        const auto newRefCount = info.getRefCount();
        LOG_DEBUG << "Уменьшен счетчик ссылок для разделяемой блокировки: " << lockPathStr
                  << ", изменение значения: " << refCount << " -> " << newRefCount;
        return true;
    }

    // Получаем дескриптор файла
    const auto fd = it->second.getFileDescriptor();
#else
    UNREACHABLE("Unsupported platform");
#endif

#if defined(OCTET_PLATFORM_UNIX)
    // Снимаем блокировку
    if (flock(fd, LOCK_UN) != 0) {
        LOG_ERROR << "Ошибка при снятии блокировки: " << lockPathStr
                  << ", ошибка: " << errnoToString(errno);
        return false;
    }

    // Закрываем файловый дескриптор
    if (close(fd) != 0) {
        LOG_ERROR << "Ошибка при закрытии файлового дескриптора: " << lockPathStr
                  << ", ошибка: " << errnoToString(errno);
        // TODO: Можем ли считать блокировку снятой?
    }

    // Удаляем файл блокировки
    if (unlink(lockPath.c_str()) != 0) {
        LOG_ERROR << "Не удалось удалить файл блокировки: " << lockPathStr
                  << ", ошибка: " << errnoToString(errno);
        // TODO: Можем ли считать блокировку снятой?
    }
#elif defined(OCTET_PLATFORM_WINDOWS)
    // Закрываем файловый дескриптор
    if (!CloseHandle(fd)) {
        LOG_ERROR << "Ошибка при закрытии файлового дескриптора: " << lockPathStr
                  << ", ошибка: " << GetLastError();
        // TODO: Можем ли считать блокировку снятой?
    }

    // Удаляем файл блокировки
    if (!DeleteFileW(lockFilePath.wstring().c_str())) {
        LOG_ERROR << "Не удалось удалить файл блокировки: " << lockFilePath.string()
                  << ", ошибка: " << GetLastError();
        // TODO: Можем ли считать блокировку снятой?
    }
#else
    UNREACHABLE("Unsupported platform");
#endif

#if defined(OCTET_PLATFORM_SUPPORTED)
    // Удаляем запись из контейнера
    fileLockMap.erase(it);

    LOG_INFO << "Блокировка успешно освобождена: " << filePath.string();
    return true;
#else
    UNREACHABLE("Unsupported platform");
#endif
}
} // namespace octet::utils
