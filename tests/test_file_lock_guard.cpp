#include <gtest/gtest.h>
#include <thread>
#include <future>
#include <vector>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <atomic>
#include <mutex>

#include "testing_utils.hpp"
#include "utils/compiler.hpp"
#include "utils/file_lock_guard.hpp"
#include "utils/file_utils.hpp"
#include "utils/logger.hpp"

namespace {
static constexpr uint8_t THREAD_START_TIMEOUT_MS = 100;
static constexpr uint8_t TIMEOUT_STRATEGY_VALUE_MS = 100;
static constexpr uint8_t TIME_EPS_FOR_LOCK = 50;
} // namespace

namespace octet::tests {
class FileLockGuardTest : public ::testing::Test {
private:
    /**
     * @brief Создаёт путь к тестовому файлу в тестовой директории
     * @param name Имя тестового файла (по умолчанию "test_file.txt")
     * @return Полный путь к тестовому файлу
     */
    std::filesystem::path getTestFilePath(const std::string &name = "test_file.txt")
    {
        return testDir / name;
    }

    /**
     * @brief Создаёт путь к блокировке для указанного файла
     * @param name Полный путь к файлу
     * @return Полный путь к блокировке для указанного файла
     */
    std::filesystem::path getLockFilePath(const std::filesystem::path &path)
    {
        return std::filesystem::path(path.string() + ".lock");
    }

protected:
    std::filesystem::path testDir; // Путь к тестовой директории

    void SetUp() override
    {
        // Создаем временную директорию
        testDir = createTmpDirectory("FileLockGuard");

        // Включаем логгер для отладки тестов
        // utils::Logger::getInstance().enable(true, std::nullopt, utils::LogLevel::DEBUG);
    }

    void TearDown() override
    {
        // Удаляем временную директорию
        removeTmpDirectory(testDir);
    }

    /**
     * @brief Одновременно создаёт путь к тестовому файлу и к его блокировке
     * @param name Имя тестового файла (по умолчанию "test_file.txt")
     * @return Полные пути к тестовому файлу и к его блокировке
     */
    std::pair<std::filesystem::path, std::filesystem::path>
    getTestAndLockPaths(const std::string &name = "test_file.txt")
    {
        const auto filePath = getTestFilePath(name);
        const auto lockPath = getLockFilePath(filePath);
        return { filePath, lockPath };
    }

    /**
     * @brief Создаёт тестовый файл с указанным содержимым
     * @param path Путь к создаваемому файлу
     * @param content Содержимое файла (по умолчанию "test content")
     */
    void createTestFile(const std::filesystem::path &path,
                        const std::string &content = "test content")
    {
        std::ofstream file(path);
        file << content;
        file.close();
        EXPECT_TRUE(std::filesystem::exists(path));
    }
};

// Проверка базовой функциональности создания и освобождения блокировки
TEST_F(FileLockGuardTest, LockTestBasicAcquireRelease)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();
    createTestFile(filePath);

    // Создаём и проверяем блокировку в отдельной области видимости
    {
        utils::FileLockGuard lock(filePath);
        EXPECT_TRUE(lock.isLocked());

        // Проверяем, что файл блокировки создан
        EXPECT_TRUE(std::filesystem::exists(lockPath));
    }

    // Проверяем, что файл блокировки удалён после уничтожения объекта блокировки
    EXPECT_FALSE(std::filesystem::exists(lockPath));
}

// Проверка явного освобождения блокировки
TEST_F(FileLockGuardTest, LockTestExplicitRelease)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();
    createTestFile(filePath);

    utils::FileLockGuard lock(filePath);
    EXPECT_TRUE(lock.isLocked());
    EXPECT_TRUE(std::filesystem::exists(lockPath));

    // Явно освобождаем блокировку
    EXPECT_TRUE(lock.release());
    EXPECT_FALSE(lock.isLocked());

    // Проверяем, что файл блокировки удалён
    EXPECT_FALSE(std::filesystem::exists(lockPath));
    // Повторный вызов release() должен вернуть false
    EXPECT_FALSE(lock.release());
}

// Проверка работы различных режимов блокировки
TEST_F(FileLockGuardTest, LockTestModes)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();
    createTestFile(filePath);

    // Тест EXCLUSIVE блокировки в одном и том же потоке
    // (должно отсекаться на уровне проверки deadlock)
    {
        // Получаем эксклюзивную блокировку
        utils::FileLockGuard exclusiveLock(filePath, utils::LockMode::EXCLUSIVE);
        EXPECT_TRUE(exclusiveLock.isLocked());

        // Пытаемся получить ещё одну EXCLUSIVE блокировку в этом же потоке
        utils::FileLockGuard anotherExclusiveLock(filePath, utils::LockMode::EXCLUSIVE,
                                                  utils::LockWaitStrategy::INSTANTLY);
        EXPECT_FALSE(anotherExclusiveLock.isLocked());

        // Пытаемся получить SHARED блокировку в этом же потоке
        utils::FileLockGuard sharedLock(filePath, utils::LockMode::SHARED,
                                        utils::LockWaitStrategy::INSTANTLY);
        EXPECT_FALSE(sharedLock.isLocked());
    }
    EXPECT_FALSE(std::filesystem::exists(lockPath));

    // Тест EXCLUSIVE блокировки в разных потоках
    {
        // Получаем эксклюзивную блокировку
        utils::FileLockGuard exclusiveLock(filePath, utils::LockMode::EXCLUSIVE);
        EXPECT_TRUE(exclusiveLock.isLocked());

        // Запускаем поток, пытающийся получить ещё одну EXCLUSIVE блокировку
        auto future1 = std::async(std::launch::async, [&filePath]() {
            utils::FileLockGuard lock(filePath, utils::LockMode::EXCLUSIVE,
                                      utils::LockWaitStrategy::INSTANTLY);
            return lock.isLocked();
        });
        // Даём потоку немного времени для запуска
        std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_START_TIMEOUT_MS));
        EXPECT_FALSE(future1.get());

        // Запускаем еще один поток, пытающийся получить SHARED блокировку
        auto future2 = std::async(std::launch::async, [&filePath]() {
            utils::FileLockGuard lock(filePath, utils::LockMode::SHARED,
                                      utils::LockWaitStrategy::INSTANTLY);
            return lock.isLocked();
        });
        // Даём потоку немного времени для запуска
        std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_START_TIMEOUT_MS));
        EXPECT_FALSE(future2.get());
    }
    EXPECT_FALSE(std::filesystem::exists(lockPath));

    // Тест SHARED блокировок в одном и том же потоке
    {
        // Получаем первую разделяемую блокировку
        utils::FileLockGuard sharedLock1(filePath, utils::LockMode::SHARED);
        EXPECT_TRUE(sharedLock1.isLocked());

        // Получаем ещё одну SHARED блокировку в этом же потоке
        utils::FileLockGuard sharedLock2(filePath, utils::LockMode::SHARED);
        EXPECT_TRUE(sharedLock2.isLocked());

        // Пытаемся получить EXCLUSIVE блокировку в этом же потоке
        // (должно отсекаться на уровне проверки deadlock)
        utils::FileLockGuard exclusiveLock(filePath, utils::LockMode::EXCLUSIVE,
                                           utils::LockWaitStrategy::INSTANTLY);
        EXPECT_FALSE(exclusiveLock.isLocked());
    }
    EXPECT_FALSE(std::filesystem::exists(lockPath));

    // Тест SHARED блокировок в разных потоках
    {
        // Получаем первую разделяемую блокировку
        utils::FileLockGuard sharedLock1(filePath, utils::LockMode::SHARED);
        EXPECT_TRUE(sharedLock1.isLocked());

        // Запускаем поток, который должен получить SHARED блокировку
        auto future1 = std::async(std::launch::async, [&filePath]() {
            utils::FileLockGuard lock(filePath, utils::LockMode::SHARED,
                                      utils::LockWaitStrategy::INSTANTLY);
            return lock.isLocked();
        });
        // Даём потоку немного времени для запуска
        std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_START_TIMEOUT_MS));
        EXPECT_TRUE(future1.get());

        // Запускаем поток, пытающийся получить EXCLUSIVE блокировку
        auto future2 = std::async(std::launch::async, [&filePath]() {
            utils::FileLockGuard lock(filePath, utils::LockMode::EXCLUSIVE,
                                      utils::LockWaitStrategy::INSTANTLY);
            return lock.isLocked();
        });
        // Даём потоку немного времени для запуска
        std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_START_TIMEOUT_MS));
        EXPECT_FALSE(future2.get());
    }
    EXPECT_FALSE(std::filesystem::exists(lockPath));
}

// Проверка работы различных стратегий ожидания
TEST_F(FileLockGuardTest, LockTestWaitStrategies)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();
    createTestFile(filePath);

    // Для INSTANTLY стратегии достаточно проверок в LockTestModes тесте

    // Стратегия TIMEOUT в одном потоке
    {
        utils::FileLockGuard lock1(filePath);
        EXPECT_TRUE(lock1.isLocked());

        // Попытка захватить блокировку с очень большим ожиданием
        const auto start = std::chrono::steady_clock::now();
        utils::FileLockGuard lock2(filePath, utils::LockMode::EXCLUSIVE,
                                   utils::LockWaitStrategy::TIMEOUT, std::chrono::minutes(10));
        const auto end = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // Блокировка должна провалиться практически сразу, так как есть риск deadlock
        EXPECT_FALSE(lock2.isLocked());

        // Проверка, что стратегия не занимает слишком много времени
        EXPECT_LT(duration.count(), TIME_EPS_FOR_LOCK);
    }
    EXPECT_FALSE(std::filesystem::exists(lockPath));

    // Стратегия TIMEOUT в разных потоках
    {
        utils::FileLockGuard lock1(filePath);
        EXPECT_TRUE(lock1.isLocked());

        // Запускаем поток, который будет ждать указанное время
        auto future = std::async(std::launch::async, [&filePath]() {
            // Эта блокировка должна ждать, пока не освободится
            const auto start = std::chrono::steady_clock::now();
            utils::FileLockGuard lock2(filePath, utils::LockMode::EXCLUSIVE,
                                       utils::LockWaitStrategy::TIMEOUT,
                                       std::chrono::milliseconds(TIMEOUT_STRATEGY_VALUE_MS));
            const auto end = std::chrono::steady_clock::now();
            const auto duration
                = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            return std::make_pair(lock2.isLocked(), duration.count());
        });

        // Даём потоку немного времени для запуска
        std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_START_TIMEOUT_MS));
        const auto [isLocked, duration] = future.get();

        EXPECT_FALSE(isLocked);
        // Проверка, что стратегия ждала не менее указанного времени
        EXPECT_GE(duration, TIMEOUT_STRATEGY_VALUE_MS);
        // Проверка, что стратегия не занимает сильно больше указанного времени
        EXPECT_LT(duration, TIMEOUT_STRATEGY_VALUE_MS + TIME_EPS_FOR_LOCK);
    }
    EXPECT_FALSE(std::filesystem::exists(lockPath));

    // Стратегия STANDARD в одном потоке
    {
        utils::FileLockGuard lock1(filePath);
        EXPECT_TRUE(lock1.isLocked());

        // Попытка захватить блокировку с бесконечным ожиданием
        const auto start = std::chrono::steady_clock::now();
        utils::FileLockGuard lock2(filePath, utils::LockMode::EXCLUSIVE,
                                   utils::LockWaitStrategy::STANDARD);
        const auto end = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // Блокировка должна провалиться практически сразу, так как есть риск deadlock
        EXPECT_FALSE(lock2.isLocked());

        // Проверка, что стратегия не занимает слишком много времени
        EXPECT_LT(duration.count(), TIME_EPS_FOR_LOCK);
    }
    EXPECT_FALSE(std::filesystem::exists(lockPath));

    // Стратегия STANDARD в разных потоках
    {
        utils::FileLockGuard lock1(filePath);
        EXPECT_TRUE(lock1.isLocked());

        // Запускаем поток, который будет ждать до получения блокировки
        auto future = std::async(std::launch::async, [&filePath]() {
            // Эта блокировка должна ждать, пока не освободится
            utils::FileLockGuard lock2(filePath, utils::LockMode::EXCLUSIVE,
                                       utils::LockWaitStrategy::STANDARD);
            return lock2.isLocked();
        });

        // Даём потоку немного времени для запуска
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Проверяем, что стратегия не завершается, пока блокировка занята
        EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);

        // Освобождаем первую блокировку
        EXPECT_TRUE(lock1.release());

        // Теперь future должен стать готовым и вернуть true
        EXPECT_TRUE(future.get());
    }
    EXPECT_FALSE(std::filesystem::exists(lockPath));
}

// Проверка работы разделяемых блокировок
TEST_F(FileLockGuardTest, LockTestSharedMode)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();
    createTestFile(filePath);

    // Создаём первую разделяемую блокировку
    utils::FileLockGuard sharedLock1(filePath, utils::LockMode::SHARED);
    EXPECT_TRUE(sharedLock1.isLocked());

    {
        // Создаём вторую разделяемую блокировку
        utils::FileLockGuard sharedLock2(filePath, utils::LockMode::SHARED);
        EXPECT_TRUE(sharedLock2.isLocked());

        // Файл блокировки должен существовать
        EXPECT_TRUE(std::filesystem::exists(lockPath));

        {
            // Создаём третью разделяемую блокировку
            utils::FileLockGuard sharedLock3(filePath, utils::LockMode::SHARED);
            EXPECT_TRUE(sharedLock3.isLocked());

            // Попытка моментально получить эксклюзивную блокировку
            utils::FileLockGuard exclusiveLock(filePath, utils::LockMode::EXCLUSIVE,
                                               utils::LockWaitStrategy::INSTANTLY);
            EXPECT_FALSE(exclusiveLock.isLocked());
        }
        // После уничтожения sharedLock3 файл блокировки должен всё ещё существовать
        EXPECT_TRUE(std::filesystem::exists(lockPath));
    }
    // После уничтожения sharedLock2 файл блокировки всё ещё должен существовать
    EXPECT_TRUE(std::filesystem::exists(lockPath));

    // Явно освобождаем первую разделяемую блокировку
    EXPECT_TRUE(sharedLock1.release());

    // Теперь файл блокировки должен быть удалён
    EXPECT_FALSE(std::filesystem::exists(lockPath));
}

// Проверка блокировки в недоступной для записи директории
TEST_F(FileLockGuardTest, LockTestInaccessibleDirectory)
{
    std::filesystem::path nonExistentDir;
#if defined(OCTET_PLATFORM_UNIX)
    nonExistentDir = std::filesystem::path("/root/nonexistent/directory");
#elif defined(OCTET_PLATFORM_WINDOWS)
    nonExistentDir = std::filesystem::path("C:/Windows/nonexistent/directory");
#else
    UNREACHABLE("Unsupported platform");
#endif
    utils::FileLockGuard lock(nonExistentDir / "octet_test.txt");
    EXPECT_FALSE(lock.isLocked());
}

// Проверка блокировки в доступной для записи директории (но несуществующей)
TEST_F(FileLockGuardTest, LockTestCreatableDirectory)
{
    const auto dirPath = testDir / "new_dir";
    const auto filePath = dirPath / "test.txt";

    // Убеждаемся, что директория не существует
    std::filesystem::remove_all(dirPath);
    EXPECT_FALSE(std::filesystem::exists(dirPath));

    // Блокировка должна создать директорию
    utils::FileLockGuard lock(filePath);
    EXPECT_TRUE(lock.isLocked());

    // Директория должна быть создана
    EXPECT_TRUE(std::filesystem::exists(dirPath));
}

// Проверка, что несколько потоков корректно конкурируют за эксклюзивную блокировку
TEST_F(FileLockGuardTest, LockTestSimpleConcurrent)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();
    createTestFile(filePath);

    constexpr size_t THREAD_COUNT = 20;
    std::vector<std::future<bool>> futures;

    // Запускаем множество потоков, пытающихся получить эксклюзивную блокировку
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        futures.push_back(std::async(std::launch::async, [this, &filePath]() {
            // Пытаемся получить эксклюзивную блокировку с таймаутом
            utils::FileLockGuard lock(filePath, utils::LockMode::EXCLUSIVE,
                                      utils::LockWaitStrategy::TIMEOUT,
                                      std::chrono::milliseconds(5000));
            EXPECT_TRUE(lock.isLocked());
            // Удерживаем блокировку случайное время между 10-100 мс
            std::this_thread::sleep_for(std::chrono::milliseconds(getRandomInt(10, 100)));
            return lock.isLocked();
        }));
    }

    // Ждём завершения всех потоков
    size_t successCount = 0;
    for (auto &future : futures) {
        if (future.get()) {
            successCount++;
        }
    }

    // Все потоки должны в итоге получить блокировку
    EXPECT_EQ(successCount, THREAD_COUNT);
    EXPECT_FALSE(std::filesystem::exists(lockPath));
}

// Проверка, что несколько потоков корректно конкурируют за разделяемые и эксклюзивные блокировки
TEST_F(FileLockGuardTest, LockTestSimpleConcurrentWithShared)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();
    createTestFile(filePath);

    constexpr size_t SHARED_THREAD_COUNT = 10;
    constexpr size_t EXCLUSIVE_THREAD_COUNT = 5;
    std::vector<std::future<bool>> sharedFutures;
    std::vector<std::future<bool>> exclusiveFutures;

    // Запускаем потоки, получающие разделяемые блокировки
    for (size_t i = 0; i < SHARED_THREAD_COUNT; i++) {
        sharedFutures.push_back(std::async(std::launch::async, [&filePath]() {
            // Так как первыми идут разделяемые блокировки, то используем INSTANTLY стратегию
            utils::FileLockGuard lock(filePath, utils::LockMode::SHARED,
                                      utils::LockWaitStrategy::INSTANTLY);
            EXPECT_TRUE(lock.isLocked());
            // Удерживаем блокировку
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return lock.isLocked();
        }));
    }

    // Запускаем потоки, пытающиеся получить эксклюзивные блокировки
    for (size_t i = 0; i < EXCLUSIVE_THREAD_COUNT; i++) {
        exclusiveFutures.push_back(std::async(std::launch::async, [&filePath]() {
            utils::FileLockGuard lock(filePath, utils::LockMode::EXCLUSIVE,
                                      utils::LockWaitStrategy::TIMEOUT,
                                      std::chrono::milliseconds(10000));
            EXPECT_TRUE(lock.isLocked());
            // Удерживаем блокировку
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return lock.isLocked();
        }));
    }

    // Ждём завершения всех потоков
    size_t sharedSuccessCount = 0;
    size_t exclusiveSuccessCount = 0;
    for (auto &future : sharedFutures) {
        if (future.get()) {
            sharedSuccessCount++;
        }
    }
    for (auto &future : exclusiveFutures) {
        if (future.get()) {
            exclusiveSuccessCount++;
        }
    }

    // Все потоки должны в итоге получить блокировку
    EXPECT_EQ(sharedSuccessCount, SHARED_THREAD_COUNT);
    EXPECT_EQ(exclusiveSuccessCount, EXCLUSIVE_THREAD_COUNT);
    EXPECT_FALSE(std::filesystem::exists(lockPath));
}

// Проверяем работу блокировки в нескольких потоках со случайными режимами и задержками
TEST_F(FileLockGuardTest, LockTestRandomParams)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();
    createTestFile(filePath);

    constexpr size_t THREAD_COUNT = 20;
    std::vector<std::future<bool>> futures;

    // Используем центральный счётчик для отслеживания, у какого потока блокировка
    // -1 = нет блокировки, [0, THREAD_COUNT - 1] = номер потока, THREAD_COUNT = разделяемая
    constexpr int8_t NON_LOCK_IND = -1;
    constexpr int8_t SHARED_LOCK_IND = static_cast<int8_t>(THREAD_COUNT);
    std::atomic<int8_t> lockHolder = NON_LOCK_IND;
    // Количество потоков с разделяемыми блокировками
    std::atomic<int8_t> sharedCount = 0;

    // Запускаем потоки, выполняющие разные операции блокировки
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        futures.push_back(
            std::async(std::launch::async, [this, i, &filePath, &lockHolder, &sharedCount]() {
                const auto mode = getRandomInt(0, 1) == 0 ? utils::LockMode::SHARED
                                                          : utils::LockMode::EXCLUSIVE;
                utils::FileLockGuard lock(filePath, mode, utils::LockWaitStrategy::TIMEOUT,
                                          std::chrono::milliseconds(getRandomInt(500, 1500)));
                EXPECT_TRUE(lock.isLocked());

                switch (mode) {
                case utils::LockMode::SHARED: {
                    sharedCount++;

                    // Если удалось захватить разделяемую блокировку, то предыдущее состояние должно
                    // соответствовать только разделяемым блокировкам или отсутствию блокировок
                    const auto currentHolder = lockHolder.load();
                    EXPECT_TRUE(currentHolder == NON_LOCK_IND || currentHolder == SHARED_LOCK_IND);

                    // Отмечаем, что удерживается разделяемая блокировка
                    lockHolder.store(SHARED_LOCK_IND);

                    // Удерживаем блокировку случайное время
                    std::this_thread::sleep_for(std::chrono::milliseconds(getRandomInt(10, 50)));

                    sharedCount--;
                    if (sharedCount.load() == 0) {
                        // Если это последняя разделяемая блокировка, то отмечаем, что
                        // блокировок не осталось
                        lockHolder.store(NON_LOCK_IND);
                    }
                    break;
                }
                case utils::LockMode::EXCLUSIVE: {
                    // Если удалось захватить эксклюзивную блокировку, то предыдущее состояние
                    // должно соответствовать только отсутствию блокировок
                    auto expected = NON_LOCK_IND;
                    // Если состояние соответствуюет ожидаемому, то заменяем его на номер потока
                    bool exchanged = lockHolder.compare_exchange_strong(expected, i);
                    EXPECT_TRUE(exchanged);

                    // Удерживаем блокировку случайное время
                    std::this_thread::sleep_for(std::chrono::milliseconds(getRandomInt(10, 30)));

                    // Перед освобождением блокировки проверяем, что после задержки текущее
                    // состояние остается равным номеру потока
                    expected = i;
                    // Если состояние соответствуюет ожидаемому, то заменяем его на состояние
                    // отсутсвия блокировок
                    exchanged = lockHolder.compare_exchange_strong(expected, -1);
                    EXPECT_TRUE(exchanged);
                    break;
                }
                default:
                    UNREACHABLE("Unsupported LockMode");
                }

                return lock.isLocked();
            }));
    }

    // Ждём завершения всех потоков
    size_t totalSuccesses = 0;
    for (auto &future : futures) {
        if (future.get()) {
            totalSuccesses++;
        }
    }

    // Все потоки должны в итоге получить блокировку
    EXPECT_EQ(totalSuccesses, THREAD_COUNT);
    EXPECT_FALSE(std::filesystem::exists(lockPath));
}

// Проверяем работу блокировки в нескольких потоках со случайными режимами, стратегиями и задержками
// (это должно быть имитацией нормальной работы, поэтому просто проверяем, что ничего не падает)
TEST_F(FileLockGuardTest, LockTestRandomLockUnlock)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();
    createTestFile(filePath);

    constexpr size_t NUM_THREADS = 8;
    constexpr size_t OPERATIONS_PER_THREAD = 20;
    std::vector<std::future<void>> futures;

    for (size_t i = 0; i < NUM_THREADS; i++) {
        futures.push_back(std::async(std::launch::async, [this, &filePath]() {
            for (size_t j = 0; j < OPERATIONS_PER_THREAD; j++) {
                // Случайно выбираем тип операции
                const auto operation = getRandomInt(0, 2);

                switch (operation) {
                // EXCLUSIVE + TIMEOUT
                case 0: {
                    utils::FileLockGuard lock(filePath, utils::LockMode::EXCLUSIVE,
                                              utils::LockWaitStrategy::TIMEOUT,
                                              std::chrono::milliseconds(getRandomInt(100, 300)));
                    if (lock.isLocked()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(getRandomInt(5, 20)));
                    }
                    break;
                }
                // SHARED + TIMEOUT
                case 1: {
                    utils::FileLockGuard lock(filePath, utils::LockMode::SHARED,
                                              utils::LockWaitStrategy::TIMEOUT,
                                              std::chrono::milliseconds(getRandomInt(100, 300)));
                    if (lock.isLocked()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(getRandomInt(5, 20)));
                    }
                    break;
                }
                // EXCLUSIVE/SHARED + INSTANTLY
                case 2: {
                    utils::FileLockGuard lock(filePath,
                                              getRandomInt(0, 1) ? utils::LockMode::EXCLUSIVE
                                                                 : utils::LockMode::SHARED,
                                              utils::LockWaitStrategy::INSTANTLY);
                    if (lock.isLocked()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(getRandomInt(1, 10)));
                    }
                    break;
                }
                }

                // Небольшая пауза между операциями
                std::this_thread::sleep_for(std::chrono::milliseconds(getRandomInt(10, 30)));
            }
        }));
    }

    // Ждём завершения всех потоков
    for (auto &future : futures) {
        future.wait();
    }
    EXPECT_FALSE(std::filesystem::exists(lockPath));
}

// Стресс-тест с большим количеством файлов и блокировок
TEST_F(FileLockGuardTest, LockTestMultipleFilesStress)
{
    constexpr size_t NUM_FILES = 100;
    constexpr size_t NUM_THREADS = 20;
    constexpr size_t OPERATIONS_PER_THREAD = 100;

    // Создаём множество файлов
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> paths;
    for (size_t i = 0; i < NUM_FILES; i++) {
        const auto currentPaths = getTestAndLockPaths("test_file_" + std::to_string(i) + ".txt");
        createTestFile(currentPaths.first);
        paths.push_back(std::move(currentPaths));
    }

    // Мьютекс для защиты доступа к списку файлов
    std::mutex filesMutex;
    size_t nextFileIndex = 0;

    // Функция для получения следующего файла для блокировки
    auto getNextFile = [&]() -> std::filesystem::path {
        std::lock_guard<std::mutex> lock(filesMutex);
        if (nextFileIndex >= paths.size()) {
            // Для циклического перебора файлов
            nextFileIndex = 0;
        }
        return paths[nextFileIndex++].first;
    };

    // Счётчик успешных и неудачных блокировок
    std::atomic<size_t> successCount(0);
    std::atomic<size_t> failureCount(0);

    // Запускаем потоки для выполнения операций блокировки
    std::vector<std::future<void>> futures;
    for (size_t i = 0; i < NUM_THREADS; i++) {
        futures.push_back(std::async(std::launch::async, [&]() {
            // Выполняем случайные операции блокировки
            for (size_t j = 0; j < OPERATIONS_PER_THREAD; j++) {
                auto waitStrategy = static_cast<utils::LockWaitStrategy>(getRandomInt(0, 2));
                auto lockMode
                    = getRandomInt(0, 1) ? utils::LockMode::EXCLUSIVE : utils::LockMode::SHARED;
                utils::FileLockGuard lock(getNextFile(), lockMode, waitStrategy,
                                          std::chrono::milliseconds(getRandomInt(50, 200)));

                if (lock.isLocked()) {
                    successCount++;
                    // Удерживаем блокировку очень короткое время
                    std::this_thread::sleep_for(std::chrono::milliseconds(getRandomInt(1, 5)));
                }
                else {
                    failureCount++;
                }
            }
        }));
    }

    // Ждём завершения всех потоков
    for (auto &future : futures) {
        future.wait();
    }

    // Проверяем, что были успешные блокировки
    EXPECT_GT(successCount, 0);

    // Проверяем, что все файлы блокировок удалены
    for (const auto currentPaths : paths) {
        EXPECT_FALSE(std::filesystem::exists(currentPaths.second));
    }
}

// Проверка освобождения блокировки другим потоком
TEST_F(FileLockGuardTest, LockTestReleaseFromOtherThread)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();
    createTestFile(filePath);

    // Получаем блокировку в текущем потоке
    utils::FileLockGuard lock(filePath);
    EXPECT_TRUE(lock.isLocked());

    // Пытаемся освободить блокировку из другого потока
    auto future = std::async(std::launch::async, [&filePath, &lock]() {
        // Пытаемся освободить блокировку напрямую с помощью статического метода
        const auto directResult = utils::FileLockGuard::releaseFileLock(filePath);
        EXPECT_FALSE(directResult);
        // Пытаемся освободить блокировку с помощью исходного объекта с блокировкой
        const auto objectResult = lock.release();
        EXPECT_FALSE(objectResult);
        return directResult || objectResult;
    });
    EXPECT_FALSE(future.get());

    // Сама блокировка должна по-прежнему быть действительной
    EXPECT_TRUE(lock.isLocked());
    // Освобождаем блокировку
    EXPECT_TRUE(lock.release());
    EXPECT_FALSE(std::filesystem::exists(lockPath));
}

// Проверка возможности создания блокировки для файла, который ещё не существует
TEST_F(FileLockGuardTest, LockTestNonExistentFile)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();

    // Убеждаемся, что файл не существует
    std::filesystem::remove(filePath);
    EXPECT_FALSE(std::filesystem::exists(filePath));

    // Получаем блокировку для несуществующего файла
    utils::FileLockGuard lock(filePath);
    EXPECT_TRUE(lock.isLocked());

    // Файл блокировки должен существовать
    EXPECT_TRUE(std::filesystem::exists(lockPath));

    // Освобождаем блокировку
    EXPECT_TRUE(lock.release());
    EXPECT_FALSE(std::filesystem::exists(lockPath));
}

// Проверка блокировки с учётом особенностей платформы
// TODO: стоит улучшить этот тест
TEST_F(FileLockGuardTest, LockTestPlatformSpecificBehavior)
{
    const auto [filePath, lockPath] = getTestAndLockPaths();
    createTestFile(filePath);

    utils::FileLockGuard lock(filePath);
    EXPECT_TRUE(lock.isLocked());
    EXPECT_TRUE(std::filesystem::exists(lockPath));

#if defined(OCTET_PLATFORM_UNIX)
    // Попытка удалить файл блокировки извне
    std::error_code ec;
    std::filesystem::remove(lockPath, ec);

    // В Unix-системах файла не должно быть видно в файловой системе после удаления,
    // но он все еще доступен через открытый дескриптор
    EXPECT_FALSE(std::filesystem::exists(lockPath));
    EXPECT_TRUE(lock.isLocked());

#elif defined(OCTET_PLATFORM_WINDOWS)
    // Попытка открыть файл извне
    std::ofstream file(lockPath);
    EXPECT_FALSE(file.is_open());
#else
    UNREACHABLE("Unsupported platform");
#endif

    // Освобождаем блокировку
    EXPECT_TRUE(lock.release());
    EXPECT_FALSE(std::filesystem::exists(lockPath));
}
} // namespace octet::tests
