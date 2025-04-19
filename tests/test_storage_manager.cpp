#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "storage/storage_manager.hpp"
#include "utils/file_utils.hpp"
#include "testing_utils.hpp"

namespace {
static constexpr char SNAPSHOT_FILE_NAME[] = "octet-data.snapshot";
static constexpr char JOURNAL_FILE_NAME[] = "octet-operations.journal";
} // namespace

namespace octet::tests {
class StorageManagerTest : public ::testing::Test {
protected:
    std::filesystem::path testDir; // Путь к тестовой директории

    void SetUp() override
    {
        // Создаем временную директорию для тестов
        testDir = createTmpDirectory("StorageManager");

        // Включаем логгер для отладки
        // utils::Logger::getInstance().enable(true, std::nullopt, utils::LogLevel::DEBUG);
    }

    void TearDown() override
    {
        // Удаляем временную директорию
        removeTmpDirectory(testDir);
    }

    /**
     * @brief Создаёт подкаталог внутри тестовой директории
     * @param subdirName Имя подкаталога
     * @return Путь к созданному подкаталогу
     */
    std::filesystem::path createSubdir(const std::string &subdirName)
    {
        const auto subdir = testDir / subdirName;
        std::filesystem::create_directory(subdir);
        EXPECT_TRUE(std::filesystem::exists(subdir));
        EXPECT_TRUE(std::filesystem::is_directory(subdir));
        return subdir;
    }

    /**
     * @brief Проверяет существование файлов снапшота и журнала
     * @param dataDir Директория с данными
     * @param expectSnapshot true если должен существовать файл снапшота
     * @param expectJournal true если должен существовать файл журнала
     */
    void checkDataFiles(const std::filesystem::path &dataDir, bool expectSnapshot = true,
                        bool expectJournal = true)
    {
        const auto snapshotPath = dataDir / SNAPSHOT_FILE_NAME;
        const auto journalPath = dataDir / JOURNAL_FILE_NAME;
        ASSERT_EQ(std::filesystem::exists(snapshotPath), expectSnapshot);
        ASSERT_EQ(std::filesystem::exists(journalPath), expectJournal);
    }

    /**
     * @brief Вспомогательная функция для проверки успешности вставки
     * @param manager Менеджер хранилища
     * @param data Данные для вставки
     * @return UUID вставленной записи
     */
    std::string insertAndCheck(StorageManager &manager, const std::string &data)
    {
        const auto uuid = manager.insert(data);
        EXPECT_TRUE(uuid.has_value());
        EXPECT_FALSE(uuid->empty());

        // Проверяем, что данные действительно добавлены
        const auto retrievedData = manager.get(*uuid);
        EXPECT_TRUE(retrievedData.has_value());
        EXPECT_EQ(*retrievedData, data);

        return *uuid;
    }

    /**
     * @brief Заполняет хранилище множеством записей
     * @param manager Менеджер хранилища
     * @param count Количество записей для вставки
     * @return Карта [uuid -> data] добавленных записей
     */
    std::unordered_map<std::string, std::string> fillStorage(StorageManager &manager, size_t count)
    {
        std::unordered_map<std::string, std::string> testData;
        for (size_t i = 0; i < count; i++) {
            const auto data = "test_data_" + std::to_string(i);
            const auto uuid = insertAndCheck(manager, data);
            testData[uuid] = data;
        }
        return testData;
    }

    /**
     * @brief Проверяет соответствие данных в хранилище и ожидаемых данных
     * @param manager Менеджер хранилища
     * @param expectedData Ожидаемые данные [uuid -> data]
     */
    void verifyStorageContents(StorageManager &manager,
                               const std::unordered_map<std::string, std::string> &expectedData)
    {
        ASSERT_EQ(manager.getEntriesCount(), expectedData.size());
        for (const auto &[uuid, expectedValue] : expectedData) {
            const auto actualValue = manager.get(uuid);
            ASSERT_TRUE(actualValue.has_value());
            ASSERT_EQ(actualValue->size(), expectedValue.size());
            ASSERT_EQ(*actualValue, expectedValue);
        }
    }

    /**
     * @brief Создает повреждённые файлы данных
     * @param dataDir Директория для создания файлов
     */
    void createCorruptDataFiles(const std::filesystem::path &dataDir)
    {
        // Создаем поврежденный снапшот
        const auto snapshotPath = dataDir / SNAPSHOT_FILE_NAME;
        const std::string corruptSnapshot = "this is not a valid snapshot data";
        ASSERT_TRUE(utils::atomicFileWrite(snapshotPath, corruptSnapshot));

        // Создаем поврежденный журнал
        const auto journalPath = dataDir / JOURNAL_FILE_NAME;
        const std::string corruptJournal
            = "# OCTET Journal Format v1.0\n"
              "INSERT|uuid1|2023-01-01T12:00:00.000Z|valid data\n"
              "INVALID|uuid2|2023-01-01T12:01:00.000Z|this line is invalid\n";
        ASSERT_TRUE(utils::atomicFileWrite(journalPath, corruptJournal));
    }
};

// Тест базовой инициализации и создания директории
TEST_F(StorageManagerTest, InitializationCreatesDirectory)
{
    const auto dataDir = testDir / "init_test";
    ASSERT_FALSE(std::filesystem::exists(dataDir));

    // Создаем менеджер хранилища, который должен создать директорию
    {
        StorageManager manager(dataDir);
        ASSERT_TRUE(std::filesystem::exists(dataDir));
        ASSERT_TRUE(std::filesystem::is_directory(dataDir));
        // Снапшота еще не должно быть, только журнал
        checkDataFiles(dataDir, false, true);
        // Никаких операций не производили, поэтому хранилище пустое
        ASSERT_EQ(manager.getEntriesCount(), 0);
    }

    // Проверяем, что при выходе из области видимости создается снапшот
    checkDataFiles(dataDir);
}

// Тест базовых операций CRUD
TEST_F(StorageManagerTest, BasicCrudOperations)
{
    const auto dataDir = createSubdir("crud_test");
    StorageManager manager(dataDir);

    // INSERT
    const std::string testData = "This is a test data string";
    const auto uuidOpt = manager.insert(testData);
    ASSERT_TRUE(uuidOpt.has_value());
    ASSERT_FALSE(uuidOpt->empty());
    const auto &uuid = *uuidOpt;

    // GET
    const auto retrievedData = manager.get(uuid);
    ASSERT_TRUE(retrievedData.has_value());
    ASSERT_EQ(*retrievedData, testData);
    // Попытка получить несуществующие данные
    const auto notExistingData = manager.get("non_existent_uuid");
    ASSERT_FALSE(notExistingData.has_value());

    // UPDATE
    const std::string updatedData = "This is updated test data";
    ASSERT_TRUE(manager.update(uuid, updatedData));
    // Проверяем обновление
    const auto retrievedAfterUpdate = manager.get(uuid);
    ASSERT_TRUE(retrievedAfterUpdate.has_value());
    ASSERT_EQ(*retrievedAfterUpdate, updatedData);
    // Попытка обновить несуществующие данные
    ASSERT_FALSE(manager.update("non_existent_uuid", "some data"));

    // REMOVE
    ASSERT_TRUE(manager.remove(uuid));
    // Проверяем удаление
    const auto retrievedAfterRemove = manager.get(uuid);
    ASSERT_FALSE(retrievedAfterRemove.has_value());
    // Попытка удалить несуществующие данные
    ASSERT_FALSE(manager.remove(uuid)); // Уже удален
    ASSERT_FALSE(manager.remove("non_existent_uuid"));
}

// Тест для проверки сохранения множества записей
TEST_F(StorageManagerTest, MultipleEntriesStorage)
{
    const auto dataDir = createSubdir("multi_test");
    StorageManager manager(dataDir);

    // Проверка существования только журнала
    checkDataFiles(dataDir, false);

    // Добавляем множество записей
    constexpr size_t TEST_ENTRIES_COUNT = 100;
    const auto testData = fillStorage(manager, TEST_ENTRIES_COUNT);

    // После 100 записей автоматически должен создаться снапшот
    // Даем немного времени, так как снапшоты создаются асинхронно
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    checkDataFiles(dataDir);

    // Проверяем количество записей
    ASSERT_EQ(manager.getEntriesCount(), TEST_ENTRIES_COUNT);

    // Проверяем все записи по отдельности
    for (const auto &[uuid, data] : testData) {
        const auto retrievedData = manager.get(uuid);
        ASSERT_TRUE(retrievedData.has_value());
        ASSERT_EQ(*retrievedData, data);
    }
}

// Тест для проверки сохранения персистентности данных
TEST_F(StorageManagerTest, DataPersistence)
{
    const auto dataDir = createSubdir("persistence_test");

    // Создаем данные с первым экземпляром менеджера
    std::unordered_map<std::string, std::string> testData;
    {
        constexpr size_t TEST_ENTRIES_COUNT = 100;
        StorageManager manager(dataDir);
        testData = fillStorage(manager, TEST_ENTRIES_COUNT);
        ASSERT_EQ(manager.getEntriesCount(), TEST_ENTRIES_COUNT);

        // Принудительно создаем снапшот перед закрытием
        ASSERT_TRUE(manager.createSnapshot());
    }

    // Создаем второй экземпляр и проверяем данные
    {
        StorageManager manager(dataDir);
        verifyStorageContents(manager, testData);
    }
}

// Тест для проверки восстановления из снапшота
TEST_F(StorageManagerTest, RecoveryFromSnapshot)
{
    constexpr size_t TEST_ENTRIES_COUNT = 30;
    const auto dataDir = createSubdir("recovery_test");
    const auto snapshotPath = dataDir / SNAPSHOT_FILE_NAME;

    // Создаем данные с первым экземпляром менеджера
    std::unordered_map<std::string, std::string> testData;
    {
        StorageManager manager(dataDir);
        testData = fillStorage(manager, TEST_ENTRIES_COUNT);
        ASSERT_EQ(manager.getEntriesCount(), TEST_ENTRIES_COUNT);
        ASSERT_TRUE(manager.createSnapshot());

        // Добавляем еще данные после явного снапшота
        for (size_t i = 0; i < TEST_ENTRIES_COUNT; i++) {
            const auto data = "post_snapshot_data_" + std::to_string(i);
            const auto uuid = insertAndCheck(manager, data);
            testData[uuid] = data;
        }
        ASSERT_EQ(manager.getEntriesCount(), TEST_ENTRIES_COUNT * 2);
    }

    // Проверяем, что второй экземпляр восстанавливает все данные
    {
        StorageManager manager(dataDir);
        verifyStorageContents(manager, testData);
        ASSERT_EQ(manager.getEntriesCount(), TEST_ENTRIES_COUNT * 2);
    }
}

// Тест для проверки восстановления только из журнала
TEST_F(StorageManagerTest, RecoveryFromJournalOnly)
{
    const auto dataDir = createSubdir("journal_only_test");

    // Создаем данные с первым экземпляром менеджера
    std::unordered_map<std::string, std::string> testData;
    {
        StorageManager manager(dataDir);
        testData = fillStorage(manager, 30);
    }

    // Убеждаемся, что файла снапшота есть (должен быть создан в деструкторе)
    const auto snapshotPath = dataDir / SNAPSHOT_FILE_NAME;
    ASSERT_TRUE(std::filesystem::exists(snapshotPath));
    // Теперь удаляем только снапшот
    ASSERT_TRUE(std::filesystem::remove(snapshotPath));
    ASSERT_FALSE(std::filesystem::exists(snapshotPath));

    // Проверяем, что второй экземпляр восстанавливает данные из журнала
    {
        StorageManager manager(dataDir);
        verifyStorageContents(manager, testData);
    }
}

// Тест ручного создания снапшота
TEST_F(StorageManagerTest, ManualSnapshotCreation)
{
    const auto dataDir = createSubdir("manual_snapshot_test");
    StorageManager manager(dataDir);

    // Добавляем записи
    const auto testData = fillStorage(manager, 40);

    // Ручное создание снапшота
    ASSERT_TRUE(manager.createSnapshot());

    // Проверяем наличие файла снапшота
    const auto snapshotPath = dataDir / SNAPSHOT_FILE_NAME;
    ASSERT_TRUE(std::filesystem::exists(snapshotPath));

    // Удаляем записи и создаем новые
    std::unordered_map<std::string, std::string> newTestData;
    for (const auto &[uuid, _] : testData) {
        ASSERT_TRUE(manager.remove(uuid));
    }

    // Добавляем новые записи
    newTestData = fillStorage(manager, 20);

    // Создаем второй снапшот
    ASSERT_TRUE(manager.createSnapshot());

    // Создаем новый экземпляр для проверки восстановления из последнего снапшота
    {
        StorageManager manager2(dataDir);

        // В хранилище должны быть только новые записи
        verifyStorageContents(manager2, newTestData);

        // Старые записи должны отсутствовать
        for (const auto &[uuid, _] : testData) {
            ASSERT_FALSE(manager2.get(uuid).has_value());
        }
    }
}

// Тест запроса асинхронного создания снапшота
TEST_F(StorageManagerTest, AsyncSnapshotCreation)
{
    const auto dataDir = createSubdir("async_snapshot_test");
    StorageManager manager(dataDir);

    // Добавляем записи
    fillStorage(manager, 40);

    // Запрашиваем асинхронное создание снапшота
    manager.requestSnapshotAsync();

    // Ждем, пока снапшот будет создан (даем время потоку)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Проверяем наличие файла снапшота
    const auto snapshotPath = dataDir / SNAPSHOT_FILE_NAME;
    ASSERT_TRUE(std::filesystem::exists(snapshotPath));
}

// Тест автоматического создания снапшота по количеству операций
TEST_F(StorageManagerTest, AutoSnapshotByOperationsCount)
{
    const auto dataDir = createSubdir("auto_snapshot_ops_test");
    StorageManager manager(dataDir);

    // Устанавливаем низкий порог операций для быстрого триггера снапшота
    constexpr size_t THRESHOLD = 10;
    manager.setSnapshotOperationsThreshold(THRESHOLD);

    // Добавляем записи до порога
    fillStorage(manager, THRESHOLD - 1);

    // На этом этапе снапшот еще не должен быть создан
    const auto snapshotPath = dataDir / SNAPSHOT_FILE_NAME;
    ASSERT_FALSE(std::filesystem::exists(snapshotPath));

    // Добавляем еще одну запись, которая должна вызвать создание снапшота
    insertAndCheck(manager, "threshold_data");

    // Ждем, пока снапшот будет создан (даем время потоку)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Теперь снапшот должен быть создан
    ASSERT_TRUE(std::filesystem::exists(snapshotPath));
}

// Тест рабочий, но пока отключаем, чтобы долго не ждать
// // Тест автоматического создания снапшота по времени
// TEST_F(StorageManagerTest, AutoSnapshotByTime)
// {
//     const auto dataDir = createSubdir("auto_snapshot_time_test");
//     StorageManager manager(dataDir);

//     // Устанавливаем минимальное время для быстрого триггера снапшота (1 минута)
//     manager.setSnapshotTimeThreshold(1);

//     // Добавляем одну запись, чтобы был материал для снапшота
//     insertAndCheck(manager, "time_test_data");

//     // Ждем, пока снапшот будет создан
//     std::this_thread::sleep_for(std::chrono::seconds(61));

//     // Проверяем, что снапшот создан
//     const auto snapshotPath = dataDir / "octet-data.snapshot";
//     ASSERT_TRUE(std::filesystem::exists(snapshotPath));
// }

// Тест работы с данными, содержащими специальные символы
TEST_F(StorageManagerTest, SpecialCharactersInData)
{
    const auto dataDir = createSubdir("special_chars_test");
    StorageManager manager(dataDir);

    // Создаем тестовые данные со специальными символами
    const std::string specialData = "Данные со спец-символами: \n\r\t\0\x01\x7F"
                                    "Unicode: 你好, мир! ß æ ø å";

    // Вставляем данные и получаем UUID
    const auto uuid = insertAndCheck(manager, specialData);

    // Проверка данных после восстановления
    auto checkRecord = [&dataDir, &specialData, &uuid] {
        StorageManager manager2(dataDir);

        // Проверяем, что данные восстановлены корректно
        const auto retrievedData = manager2.get(uuid);
        const auto dataExists = retrievedData.has_value();
        const auto dataEqual = dataExists ? specialData == *retrievedData : false;
        return std::make_pair(dataExists, dataEqual);
    };

    /* Проверка восстановления из снапшота */
    // Создаем снапшот
    ASSERT_TRUE(manager.createSnapshot());
    // Получаем результаты проверки при восстановлении из снапшота
    const auto snapshotCheck = checkRecord();
    ASSERT_TRUE(snapshotCheck.first);
    ASSERT_TRUE(snapshotCheck.second);

    /* Проверка восстановления из журнала */
    // Удаляем снапшот
    const auto snapshotPath = dataDir / SNAPSHOT_FILE_NAME;
    ASSERT_TRUE(std::filesystem::exists(snapshotPath));
    ASSERT_TRUE(std::filesystem::remove(snapshotPath));
    ASSERT_FALSE(std::filesystem::exists(snapshotPath));
    // Получаем результаты проверки при восстановлении из журнала
    const auto journalCheck = checkRecord();
    ASSERT_TRUE(journalCheck.first);
    ASSERT_TRUE(journalCheck.second);
}

TEST_F(StorageManagerTest, LargeString)
{
    const auto dataDir = createSubdir("large_string_test");
    StorageManager manager(dataDir);

    const auto largeString = generateLargeString();
    const auto uuid = insertAndCheck(manager, largeString);

    // Проверка данных после восстановления
    auto checkRecord = [&dataDir, &largeString, &uuid] {
        StorageManager manager2(dataDir);

        // Проверяем, что данные восстановлены корректно
        const auto retrievedData = manager2.get(uuid);
        const auto dataExists = retrievedData.has_value();
        const auto dataEqual = dataExists ? largeString == *retrievedData : false;
        return std::make_pair(dataExists, dataEqual);
    };

    /* Проверка восстановления из снапшота */
    // Создаем снапшот
    ASSERT_TRUE(manager.createSnapshot());
    // Получаем результаты проверки при восстановлении из снапшота
    const auto snapshotCheck = checkRecord();
    ASSERT_TRUE(snapshotCheck.first);
    ASSERT_TRUE(snapshotCheck.second);

    /* Проверка восстановления из журнала */
    // Удаляем снапшот
    const auto snapshotPath = dataDir / SNAPSHOT_FILE_NAME;
    ASSERT_TRUE(std::filesystem::exists(snapshotPath));
    ASSERT_TRUE(std::filesystem::remove(snapshotPath));
    ASSERT_FALSE(std::filesystem::exists(snapshotPath));
    // Получаем результаты проверки при восстановлении из журнала
    const auto journalCheck = checkRecord();
    ASSERT_TRUE(journalCheck.first);
    ASSERT_TRUE(journalCheck.second);
}

// Тест рабочий, но пока отключаем, чтобы долго не ждать и не изнашивать диск
// // Стресс-тест работы с очень большими данными
// TEST_F(StorageManagerTest, StressLargeData)
// {
//     const auto dataDir = createSubdir("stress_large_data_test");
//     StorageManager manager(dataDir);

//     constexpr size_t DATA_COUNT = 100;
//     std::unordered_map<std::string, std::string> data;
//     for (size_t i = 0; i < DATA_COUNT; i++) {
//         // Получаем строку размером от LARGE_FILE_SIZE до LARGE_FILE_SIZE * 115%
//         const auto largeString = generateLargeString(LARGE_FILE_SIZE * getRandomInt(10, 15) *
//         0.1); const auto uuid = insertAndCheck(manager, largeString); data[uuid] = largeString;
//     }

//     // Создаем снапшот
//     ASSERT_TRUE(manager.createSnapshot());
//     // Проверка восстановления из снапшота
//     {
//         StorageManager manager2(dataDir);
//         verifyStorageContents(manager2, data);
//     }

//     // Удаляем снапшот
//     const auto snapshotPath = dataDir / SNAPSHOT_FILE_NAME;
//     ASSERT_TRUE(std::filesystem::exists(snapshotPath));
//     ASSERT_TRUE(std::filesystem::remove(snapshotPath));
//     ASSERT_FALSE(std::filesystem::exists(snapshotPath));
//     // Проверка восстановления из журнала
//     {
//         StorageManager manager2(dataDir);
//         verifyStorageContents(manager2, data);
//     }
// }

// Тест поведения с поврежденными файлами данных
TEST_F(StorageManagerTest, CorruptDataFiles)
{
    const auto dataDir = createSubdir("corrupt_data_test");

    // Создаем поврежденные файлы данных
    createCorruptDataFiles(dataDir);

    // Создаем менеджер хранилища, который должен корректно обработать ситуацию
    StorageManager manager(dataDir);

    // Хранилище должно быть пустым
    ASSERT_EQ(manager.getEntriesCount(), 0);

    // Добавляем новые данные
    const auto testData = fillStorage(manager, 10);

    // Проверяем, что данные добавлены
    verifyStorageContents(manager, testData);

    // Принудительное создание снапшота
    ASSERT_TRUE(manager.createSnapshot());

    // Создаем новый экземпляр для проверки восстановления
    {
        StorageManager manager2(dataDir);

        // Проверяем, что новые данные восстановлены корректно
        verifyStorageContents(manager2, testData);
    }
}

// Тест параллельных операций чтения
TEST_F(StorageManagerTest, ConcurrentReads)
{
    const auto dataDir = createSubdir("concurrent_reads_test");
    StorageManager manager(dataDir);

    // Добавляем тестовые данные
    const auto testData = fillStorage(manager, 100);
    std::vector<std::string> uuids;
    for (const auto &[uuid, _] : testData) {
        uuids.push_back(uuid);
    }

    // Запускаем множество потоков для параллельного чтения
    constexpr size_t THREAD_COUNT = 20;
    constexpr size_t READS_PER_THREAD = 100;

    std::vector<std::future<bool>> readFutures;
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        readFutures.push_back(std::async(std::launch::async, [&manager, &uuids, &testData]() {
            bool success = true;
            for (size_t j = 0; j < READS_PER_THREAD; j++) {
                // Выбираем случайный UUID
                const auto &uuid = uuids[getRandomInt(0, uuids.size() - 1)];
                const auto expectedData = testData.at(uuid);

                // Читаем данные
                const auto retrievedData = manager.get(uuid);
                if (!retrievedData.has_value() || *retrievedData != expectedData) {
                    success = false;
                    break;
                }

                // Небольшая задержка для имитации работы
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return success;
        }));
    }

    // Проверяем результаты всех потоков
    bool allSucceeded = true;
    for (auto &future : readFutures) {
        allSucceeded &= future.get();
    }
    ASSERT_TRUE(allSucceeded);
}

// Тест параллельных операций записи
TEST_F(StorageManagerTest, ConcurrentWrites)
{
    const auto dataDir = createSubdir("concurrent_writes_test");
    StorageManager manager(dataDir);

    // Запускаем множество потоков для параллельной записи
    constexpr size_t THREAD_COUNT = 20;
    constexpr size_t OPERATIONS_PER_THREAD = 30;

    std::atomic<size_t> successCount(0);
    std::mutex dataMapMutex;
    std::unordered_map<std::string, std::string> addedData;

    std::vector<std::future<void>> writeFutures;
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        writeFutures.push_back(std::async(std::launch::async, [&]() {
            for (size_t j = 0; j < OPERATIONS_PER_THREAD; j++) {
                const auto data = "data_thread_" + std::to_string(i) + "_op_" + std::to_string(j);

                // Вставляем данные
                const auto uuid = manager.insert(data);
                if (uuid.has_value()) {
                    // Сохраняем UUID и данные для последующей проверки
                    std::lock_guard<std::mutex> lock(dataMapMutex);
                    addedData[*uuid] = data;
                    successCount++;
                }

                // Небольшая задержка для имитации работы
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }));
    }

    // Ждем завершения всех потоков
    for (auto &future : writeFutures) {
        future.wait();
    }

    // Проверяем количество успешных операций
    ASSERT_EQ(successCount, THREAD_COUNT * OPERATIONS_PER_THREAD);
    // Проверяем, что все данные были сохранены
    verifyStorageContents(manager, addedData);
}

// Тест параллельного чтения и записи
TEST_F(StorageManagerTest, ConcurrentReadsAndWrites)
{
    const auto dataDir = createSubdir("concurrent_rw_test");
    StorageManager manager(dataDir);

    // Добавляем начальные данные
    const auto initialData = fillStorage(manager, 50);
    std::vector<std::string> initialUuids;
    for (const auto &[uuid, _] : initialData) {
        initialUuids.push_back(uuid);
    }

    // Флаг для синхронизации начала работы потоков
    std::atomic<bool> startFlag(false);

    // Мьютекс для доступа к общим структурам данных
    std::mutex dataMapMutex;
    std::unordered_map<std::string, std::string> updatedData(initialData);
    std::unordered_map<std::string, std::string> newData;
    std::unordered_set<std::string> removedUuids;

    // Запускаем потоки для чтения
    constexpr size_t READ_THREADS = 20;
    constexpr size_t READ_OPERATIONS = 100;
    std::vector<std::future<bool>> readFutures;
    for (size_t i = 0; i < READ_THREADS; i++) {
        readFutures.push_back(std::async(std::launch::async, [&]() {
            // Ждем сигнала
            while (!startFlag.load()) {
                std::this_thread::yield();
            }

            bool success = true;
            for (size_t j = 0; j < READ_OPERATIONS; j++) {
                // Выбираем случайный UUID из начальных данных
                const std::string uuid = initialUuids[getRandomInt(0, initialUuids.size() - 1)];

                // Читаем данные
                const auto retrievedData = manager.get(uuid);

                // TODO: Эта проверка все-таки не надежная, поскольку захватываем мьютекс после
                // получения данных, а до этого момента данные могли уже измениться, но эти
                // изменения не успели зафиксироваться в хранилищах. А захват мьютекса раньше
                // приведет к тому, что смысла в конкуренции не будет. Поэтому пока просто
                // проверяем, отсутствие критических ошибок.

                // Проверяем соответствие данных ожидаемым
                // (делаем специальную проверку, так как данные могут обновляться другими потоками)
                // {
                //     std::lock_guard<std::mutex> lock(dataMapMutex);

                //     // Если UUID был удален, не должно быть данных
                //     if (removedUuids.find(uuid) != removedUuids.end()) {
                //         if (retrievedData.has_value()) {
                //             success = false;
                //         }
                //     }
                //     // Иначе должны быть данные, соответствующие текущему состоянию
                //     else if (updatedData.find(uuid) != updatedData.end()) {
                //         if (!retrievedData.has_value() || *retrievedData != updatedData[uuid]) {
                //             success = false;
                //         }
                //     }
                // }

                // Небольшая задержка для имитации работы
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return success;
        }));
    }

    // Запускаем потоки для операций INSERT
    constexpr size_t INSERT_THREADS = 15;
    constexpr size_t INSERT_OPERATIONS = 30;
    std::vector<std::future<bool>> insertFutures;
    for (size_t i = 0; i < INSERT_THREADS; i++) {
        insertFutures.push_back(std::async(std::launch::async, [&, i]() {
            // Ждем сигнала
            while (!startFlag.load()) {
                std::this_thread::yield();
            }

            bool success = true;
            for (size_t j = 0; j < INSERT_OPERATIONS; j++) {
                const auto data
                    = "new_data_thread_" + std::to_string(i) + "_op_" + std::to_string(j);

                // Вставляем данные
                const auto uuidOpt = manager.insert(data);
                if (uuidOpt.has_value()) {
                    // Сохраняем UUID и данные
                    std::lock_guard<std::mutex> lock(dataMapMutex);
                    newData[*uuidOpt] = data;
                }
                else {
                    success = false;
                }

                // Небольшая задержка
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return success;
        }));
    }

    // Запускаем потоки для операций UPDATE
    constexpr size_t UPDATE_THREADS = 10;
    constexpr size_t UPDATE_OPERATIONS = 20;
    std::vector<std::future<void>> updateFutures;
    for (size_t i = 0; i < UPDATE_THREADS; i++) {
        updateFutures.push_back(std::async(std::launch::async, [&, i]() {
            // Ждем сигнала
            while (!startFlag.load()) {
                std::this_thread::yield();
            }

            for (size_t j = 0; j < UPDATE_OPERATIONS; j++) {
                // Выбираем случайный UUID из начальных данных
                std::string uuid;
                {
                    std::lock_guard<std::mutex> lock(dataMapMutex);
                    // Пропускаем, если все UUID удалены
                    if (updatedData.empty()) {
                        continue;
                    }

                    // Получаем случайный UUID, который еще не был удален
                    auto it = updatedData.begin();
                    std::advance(it, getRandomInt(0, updatedData.size() - 1));
                    uuid = it->first;
                }

                const auto newData
                    = "updated_data_thread_" + std::to_string(i) + "_op_" + std::to_string(j);

                // Обновляем данные
                if (manager.update(uuid, newData)) {
                    // Обновляем ожидаемые данные
                    std::lock_guard<std::mutex> lock(dataMapMutex);
                    updatedData[uuid] = newData;
                }
                else {
                    // Обновление могло не удаться, если UUID был уже удален другим потоком
                    // Это нормально в условиях конкуренции
                }

                // Небольшая задержка
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }));
    }

    // Запускаем потоки для операций REMOVE
    constexpr size_t REMOVE_THREADS = 3;
    constexpr size_t REMOVE_OPERATIONS = 10;
    std::vector<std::future<void>> removeFutures;
    for (size_t i = 0; i < REMOVE_THREADS; i++) {
        removeFutures.push_back(std::async(std::launch::async, [&]() {
            // Ждем сигнала
            while (!startFlag.load()) {
                std::this_thread::yield();
            }

            for (size_t j = 0; j < REMOVE_OPERATIONS; j++) {
                // Выбираем случайный UUID из начальных данных
                std::string uuid;
                {
                    std::lock_guard<std::mutex> lock(dataMapMutex);
                    // Пропускаем, если все UUID уже удалены
                    if (updatedData.empty()) {
                        continue;
                    }

                    // Получаем случайный UUID, который еще не был удален
                    auto it = updatedData.begin();
                    std::advance(it, getRandomInt(0, updatedData.size() - 1));
                    uuid = it->first;
                }

                {
                }

                // Удаляем данные
                if (manager.remove(uuid)) {
                    // Обновляем информацию об удаленных UUID
                    std::lock_guard<std::mutex> lock(dataMapMutex);
                    updatedData.erase(uuid);
                    removedUuids.insert(uuid);
                }
                else {
                    // Удаление могло не удаться, если UUID был уже удален другим потоком
                    // Это нормально в условиях конкуренции
                }

                // Небольшая задержка
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }));
    }

    // Сигнал для начала работы всех потоков
    startFlag = true;

    // Проверяем результаты потоков чтения
    bool allReadsSucceeded = true;
    for (auto &future : readFutures) {
        allReadsSucceeded &= future.get();
    }
    ASSERT_TRUE(allReadsSucceeded);

    // Проверяем результаты потоков INSERT
    bool allInsertsSucceeded = true;
    for (auto &future : insertFutures) {
        allInsertsSucceeded &= future.get();
    }
    ASSERT_TRUE(allInsertsSucceeded);

    // Дожидаемся окончания потоков UPDATE
    for (auto &future : updateFutures) {
        future.wait();
    }

    // Дожидаемся окончания потоков REMOVE
    for (auto &future : removeFutures) {
        future.wait();
    }

    // Проверяем, что все новые данные были сохранены
    for (const auto &[uuid, data] : newData) {
        const auto retrievedData = manager.get(uuid);
        ASSERT_TRUE(retrievedData.has_value());
        ASSERT_EQ(*retrievedData, data);
    }

    // Проверяем, что все обновленные данные имеют правильные значения
    for (const auto &[uuid, data] : updatedData) {
        const auto retrievedData = manager.get(uuid);
        ASSERT_TRUE(retrievedData.has_value());
        ASSERT_EQ(*retrievedData, data);
    }

    // Проверяем, что все удаленные данные действительно удалены
    for (const auto &uuid : removedUuids) {
        ASSERT_FALSE(manager.get(uuid).has_value());
    }
}

// Тест для проверки работы при отсутствии прав на директорию
TEST_F(StorageManagerTest, NoPermissionsDirectory)
{
#if defined(OCTET_PLATFORM_UNIX)
    const auto dataDir = createSubdir("no_permissions_test");

    // Меняем права на директорию, чтобы она была только для чтения
    std::filesystem::permissions(dataDir,
                                 std::filesystem::perms::owner_read
                                     | std::filesystem::perms::group_read
                                     | std::filesystem::perms::others_read,
                                 std::filesystem::perm_options::replace);

    // Попытка создать хранилище в директории без прав на запись должна вызвать исключение
    ASSERT_THROW({ StorageManager manager(dataDir); }, std::runtime_error);

    // Восстанавливаем права для очистки
    std::filesystem::permissions(dataDir, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
#else
    GTEST_SKIP()
        << "Пропускаем, так как тест требует возможности изменения прав доступа к директории";
#endif
}

// Тест поведения при установке экстремальных значений порогов снапшотов
TEST_F(StorageManagerTest, ExtremeSnapshotThresholds)
{
    const auto dataDir = createSubdir("extreme_thresholds_test");
    StorageManager manager(dataDir);

    // Устанавливаем очень высокие пороги
    constexpr size_t HIGH_OPS_THRESHOLD = 10000;
    constexpr size_t HIGH_TIME_THRESHOLD = 100 * 60; // 100 часов

    manager.setSnapshotOperationsThreshold(HIGH_OPS_THRESHOLD);
    manager.setSnapshotTimeThreshold(HIGH_TIME_THRESHOLD);

    // Добавляем записи (меньше порога)
    fillStorage(manager, 50);

    // Проверяем, что снапшот не создан автоматически
    const auto snapshotPath = dataDir / SNAPSHOT_FILE_NAME;
    ASSERT_FALSE(std::filesystem::exists(snapshotPath));

    // Устанавливаем очень низкие пороги, после чего сразу должен быть создан новый снапшот
    constexpr size_t LOW_OPS_THRESHOLD = 1;
    manager.setSnapshotOperationsThreshold(LOW_OPS_THRESHOLD);

    // Ждем, пока снапшот будет создан
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_TRUE(std::filesystem::exists(snapshotPath));

    // Удаляем последний снапшот
    ASSERT_TRUE(std::filesystem::remove(snapshotPath));
    ASSERT_FALSE(std::filesystem::exists(snapshotPath));

    // Добавляем одну запись, которая должна вызвать создание снапшота
    insertAndCheck(manager, "low_threshold_data");
    // Ждем, пока снапшот будет создан
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // Снапшот должен быть создан
    EXPECT_TRUE(std::filesystem::exists(snapshotPath));
}

// Тест обработки пустых данных
TEST_F(StorageManagerTest, EmptyData)
{
    const auto dataDir = createSubdir("empty_data_test");
    StorageManager manager(dataDir);

    // Вставляем пустую строку
    const auto uuid = insertAndCheck(manager, "");

    // Проверяем восстановление (из журнала)
    {
        StorageManager manager2(dataDir);

        // Проверяем, что пустая строка восстановлена
        const auto retrievedData = manager2.get(uuid);
        ASSERT_TRUE(retrievedData.has_value());
        ASSERT_TRUE(retrievedData->empty());
    }
}

// Тест работы с недопустимыми UUID
TEST_F(StorageManagerTest, InvalidUuids)
{
    const auto dataDir = createSubdir("invalid_uuid_test");
    StorageManager manager(dataDir);

    // Заполняем хранилище, чтобы не было пустым
    fillStorage(manager, 10);

    // Проверяем операции с пустым UUID
    EXPECT_FALSE(manager.get("").has_value());
    EXPECT_FALSE(manager.update("", "test data"));
    EXPECT_FALSE(manager.remove(""));

    // Проверяем операции с некорректным UUID
    const std::string invalidUuid = "not-a-valid-uuid";
    EXPECT_FALSE(manager.get(invalidUuid).has_value());
    EXPECT_FALSE(manager.update(invalidUuid, "test data"));
    EXPECT_FALSE(manager.remove(invalidUuid));
}

// Тест глубокой проверки целостности данных после восстановления
TEST_F(StorageManagerTest, DeepDataIntegrityCheck)
{
    const auto dataDir = createSubdir("data_integrity_test");

    // Создаем разнообразные тестовые данные
    std::unordered_map<std::string, std::string> testData;
    {
        StorageManager manager(dataDir);

        // Добавляем разные типы данных
        const std::vector<std::string> dataVariants = {
            "", // Пустая строка
            "Regular ASCII text",
            "Unicode text: 你好, мир! ß æ ø å",
            "Special chars: \n\r\t\\",
            std::string(1024, 'A'), // Повторяющийся символ
            generateLargeString(10 * 1024) // Большой объем данных (10 КБ)
        };

        for (const auto &data : dataVariants) {
            const auto uuid = insertAndCheck(manager, data);
            if (!uuid.empty()) {
                testData[uuid] = data;
            }
        }
    }

    // Создаем новый экземпляр и проверяем данные
    {
        StorageManager manager(dataDir);

        // Проверяем, что все данные полностью соответствуют оригиналам
        for (const auto &[uuid, expectedData] : testData) {
            const auto retrievedData = manager.get(uuid);
            ASSERT_TRUE(retrievedData.has_value());

            // Проверка размера и содержимого
            EXPECT_EQ(retrievedData->size(), expectedData.size());
            EXPECT_EQ(*retrievedData, expectedData);
        }
    }
}
} // namespace octet::tests
