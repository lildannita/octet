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
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

#include "testing_utils.hpp"
#include "utils/compiler.hpp"
#include "utils/file_utils.hpp"
#include "utils/file_lock_guard.hpp"
#include "utils/logger.hpp"
#include "storage/journal_manager.hpp"
#include "utils/compiler.hpp"

namespace octet::tests {
class JournalManagerTest : public ::testing::Test {
protected:
    std::filesystem::path testDir; // Путь к тестовой директории

    void SetUp() override
    {
        // Создаем временную директорию для тестов
        testDir = createTmpDirectory("JournalManager");

        // Включаем логгер для отладки
        // utils::Logger::getInstance().enable(true, std::nullopt, utils::LogLevel::DEBUG);
    }

    void TearDown() override
    {
        // Удаляем временную директорию
        removeTmpDirectory(testDir);
    }

    /**
     * @brief Получает путь к тестовому журналу в тестовой директории
     * @param dirPath Путь к директории, отличной от исходной (по умолчанию пустой)
     * @param name Имя тестового журнала (по умолчанию "test_journal.log")
     * @return Полный путь к тестовому журналу
     */
    std::filesystem::path getTestJournalPath(const std::optional<std::filesystem::path> dirPath
                                             = std::nullopt,
                                             const std::string &fileName = "test_journal.log")
    {
        return (dirPath.has_value() ? *dirPath : testDir) / fileName;
    }

    /**
     * @brief Проверяет, что запись в журнале существует
     * @param journalPath Путь к журналу
     * @param pattern Строка для поиска в журнале
     * @return true, если запись найдена
     */
    bool journalContains(const std::filesystem::path &journalPath, const std::string &pattern)
    {
        std::string content;
        EXPECT_TRUE(utils::safeFileRead(journalPath, content));
        return content.find(pattern) != std::string::npos;
    }

    /**
     * @brief Заполняет журнал тестовыми данными
     * @param journal Ссылка на экземпляр журнала
     * @param numOperations Количество операций каждого типа
     * @return Хранилище данных в итоговом состоянии [uuid -> data]
     */
    std::unordered_map<std::string, std::string> fillTestJournal(JournalManager &journal,
                                                                 size_t numOperations = 10)
    {
        std::unordered_map<std::string, std::string> testData;

        // Добавляем операции INSERT
        for (size_t i = 0; i < numOperations; i++) {
            const auto uuid = "uuid_insert_" + std::to_string(i);
            const auto data = "data_insert_" + std::to_string(i);
            EXPECT_TRUE(journal.writeInsert(uuid, data));
            testData[uuid] = data;
        }

        // Добавляем контрольную точку
        const auto checkpoint1 = "checkpoint_1";
        EXPECT_TRUE(journal.writeCheckpoint(checkpoint1));

        // Добавляем операции UPDATE
        for (size_t i = 0; i < numOperations / 2; i++) {
            const auto uuid = "uuid_insert_" + std::to_string(i);
            const auto data = "data_updated_" + std::to_string(i);
            EXPECT_TRUE(journal.writeUpdate(uuid, data));
            testData[uuid] = data;
        }

        // Добавляем операции REMOVE
        for (size_t i = numOperations / 2; i < numOperations; i++) {
            const auto uuid = "uuid_insert_" + std::to_string(i);
            EXPECT_TRUE(journal.writeRemove(uuid));
            testData.erase(uuid);
        }

        // Добавляем вторую контрольную точку
        const auto checkpoint2 = "checkpoint_2";
        EXPECT_TRUE(journal.writeCheckpoint(checkpoint2));

        return testData;
    }

    /**
     * @brief Создает поврежденный журнал для тестирования
     * @param journalPath Путь к журналу
     * @return Путь к созданному журналу
     */
    void createCorruptJournal(const std::filesystem::path &journalPath)
    {
        std::ostringstream oss;
        oss << "# OCTET Journal Format v1.0\n\n";
        oss << "INSERT|uuid1|2023-01-01T12:00:00.000Z|valid data\n";
        oss << "INVALID|uuid2|2023-01-01T12:01:00.000Z|this line is invalid\n";
        oss << "UPDATE|uuid1|2023-01-01T12:02:00.000Z|more valid data\n";
        EXPECT_TRUE(utils::atomicFileWrite(journalPath, oss.str()));
    }

    /**
     * @brief Подсчитывает количество резервных копий в директории
     * @param dirPath Путь к директории (если не указан, то используется исходная директория)
     * @return Количество резервных копий
     */
    size_t countBackups(const std::optional<std::filesystem::path> &dirPath = std::nullopt)
    {
        size_t backupCount = 0;
        for (const auto &entry :
             std::filesystem::directory_iterator(dirPath.has_value() ? *dirPath : testDir)) {
            const auto path = entry.path();
            if (path.string().find(".backup.") != std::string::npos) {
                backupCount++;
            }
        }
        return backupCount;
    }
};

// Тест базовой инициализации журнала
TEST_F(JournalManagerTest, JournalInitializationBasic)
{
    const auto journalPath = getTestJournalPath();
    EXPECT_FALSE(std::filesystem::exists(journalPath));

    {
        JournalManager journal(journalPath);
        // Журнал должен быть создан при инициализации
        EXPECT_TRUE(std::filesystem::exists(journalPath));
        // Журнал должен быть валидным
        EXPECT_TRUE(journal.isJournalValid());
        // Изначально в журнале нет контрольных точек
        EXPECT_FALSE(journal.getLastCheckpointId().has_value());
    }

    // Проверяем, что в журнале есть заголовок
    EXPECT_TRUE(journalContains(journalPath, "# OCTET Journal Format v1.0"));
}

// Тест инициализации журнала с существующим файлом
TEST_F(JournalManagerTest, JournalInitializationExisting)
{
    const auto journalPath = getTestJournalPath();
    EXPECT_FALSE(std::filesystem::exists(journalPath));

    // Создаем и закрываем первый экземпляр журнала
    {
        JournalManager journal(journalPath);
        EXPECT_TRUE(std::filesystem::exists(journalPath));
        EXPECT_TRUE(journal.isJournalValid());
    }

    // Создаем второй экземпляр журнала, и проверяем, что все в порядке
    {
        JournalManager journal(journalPath);
        EXPECT_TRUE(journal.isJournalValid());
    }
}

// Тест обработки поврежденного журнала
TEST_F(JournalManagerTest, JournalInitializationCorrupt)
{
    const auto journalPath = getTestJournalPath();
    EXPECT_FALSE(std::filesystem::exists(journalPath));

    // Проверяем, что никаких бэкапов в директории нет
    EXPECT_EQ(countBackups(), 0);

    // Создаем поврежденный журнал
    createCorruptJournal(journalPath);
    EXPECT_TRUE(std::filesystem::exists(journalPath));

    // При прямом (через atomicFileWrite) создании поврежденного журнала никаких бэкапов
    // не должно было создаться
    EXPECT_EQ(countBackups(), 0);

    // При инициализации поврежденного журнала должна быть создана резервная копия
    // и инициализирован новый журнал
    JournalManager journal(journalPath);
    EXPECT_TRUE(journal.isJournalValid());
    EXPECT_EQ(countBackups(), 1);
}

// Тест инициализации журнала с некорректным форматом и ошибкой при создании резервной копии
TEST_F(JournalManagerTest, JournalInitializationInReadOnlyDir)
{
#if defined(OCTET_PLATFORM_UNIX)
    const auto dirPath = testDir / "newDir";
    const auto journalPath = getTestJournalPath(dirPath);
    EXPECT_FALSE(std::filesystem::exists(dirPath));
    EXPECT_FALSE(std::filesystem::exists(journalPath));

    // Создаем поврежденный журнал
    createCorruptJournal(journalPath);
    EXPECT_TRUE(std::filesystem::exists(dirPath));
    EXPECT_TRUE(std::filesystem::is_directory(dirPath));
    EXPECT_TRUE(std::filesystem::exists(journalPath));

    // Устанавливаем права только для чтения для директории
    std::filesystem::permissions(dirPath,
                                 std::filesystem::perms::owner_read
                                     | std::filesystem::perms::group_read
                                     | std::filesystem::perms::others_read,
                                 std::filesystem::perm_options::replace);

    // Проверяем, что директория осталась доступной для чтения
    EXPECT_TRUE(std::filesystem::exists(dirPath));
    EXPECT_TRUE(std::filesystem::is_directory(dirPath));

    // Проверяем, что конструктор выбрасывает исключение
    EXPECT_THROW({ JournalManager journal(journalPath); }, std::runtime_error);
    // Проверяем, что никакие бэкапы не созданы
    EXPECT_EQ(countBackups(dirPath), 0);

    // Восстанавливаем права для корректного завершения теста
    std::filesystem::permissions(dirPath, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
#else
    GTEST_SKIP()
        << "Пропускаем, так как тест требует возможности изменения прав доступа к директории";
#endif
}

// TODO: временно убрали, так как почему-то иногда тесты прерываются без какой-либо ошибки
// Тест инициализации журнала в системной защищенной директории
// TEST_F(JournalManagerTest, JournalInitializationInSystemDir)
// {
// #if defined(OCTET_PLATFORM_UNIX)
//     const auto journalPath = std::filesystem::path("/root/octet_test_journal.log");
//     EXPECT_THROW({ JournalManager journal(journalPath); }, std::runtime_error);
// #elif defined(OCTET_PLATFORM_WINDOWS)
//     const auto journalPath =
//     std::filesystem::path("C:\\Windows\\System32\\octet_test_journal.log"); EXPECT_THROW({
//     JournalManager journal(journalPath); }, std::runtime_error);
// #else
//     UNREACHABLE("Unsupported platform");
// #endif
// }

// Тест инициализации журнала с защищенным от записи файлом
TEST_F(JournalManagerTest, JournalInitializationReadOnlyFile)
{
#if defined(OCTET_PLATFORM_UNIX)
    const auto journalPath = getTestJournalPath();
    EXPECT_FALSE(std::filesystem::exists(journalPath));

    // Создаем поврежденный журнал
    createCorruptJournal(journalPath);
    EXPECT_TRUE(std::filesystem::exists(journalPath));

    // Делаем файл журнала только для чтения
    std::filesystem::permissions(journalPath,
                                 std::filesystem::perms::owner_read
                                     | std::filesystem::perms::group_read
                                     | std::filesystem::perms::others_read,
                                 std::filesystem::perm_options::replace);

    // Так как при перезаписи журнала мы не трогаем исходный файл, а создаем временный,
    // записываем в него данные, а затем переименовываем временный в исходный файл, то никаких
    // проблем из-за прав доступа не должно быть, поскольку операция переименования
    // (std::filesystem::rename) в UNIX-системах проверяет права доступа к директории, а не к
    // самому файлу
    JournalManager journal(journalPath);
    EXPECT_TRUE(journal.isJournalValid());

    // Проверяем, что создан бэкап
    EXPECT_EQ(countBackups(), 1);
#else
    GTEST_SKIP()
        << "Пропускаем, так как тест требует возможности изменения прав доступа к директории";
#endif
}

// Тест базовых операций записи и проверки валидности
TEST_F(JournalManagerTest, BasicOperations)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    // Проверяем запись операции INSERT
    const std::string uuid = "test_uuid";
    const std::string insertData = "test_data";
    EXPECT_TRUE(journal.writeOperation(OperationType::INSERT, uuid, insertData));
    EXPECT_TRUE(journalContains(journalPath, "INSERT|" + uuid));

    // Проверяем запись операции UPDATE
    const std::string updateData = "updated_data";
    EXPECT_TRUE(journal.writeOperation(OperationType::UPDATE, uuid, updateData));
    EXPECT_TRUE(journalContains(journalPath, "UPDATE|" + uuid));

    // Проверяем запись операции REMOVE
    EXPECT_TRUE(journal.writeOperation(OperationType::REMOVE, uuid));
    EXPECT_TRUE(journalContains(journalPath, "REMOVE|" + uuid));

    // Проверяем запись контрольной точки
    const std::string checkpointId = "checkpoint_test";
    EXPECT_TRUE(journal.writeCheckpoint(checkpointId));
    EXPECT_TRUE(journalContains(journalPath, "CHECKPOINT|" + checkpointId));
}

// Тест базового воспроизведения журнала
TEST_F(JournalManagerTest, ReplayJournalBasic)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    // Заполняем журнал тестовыми данными и получаем хранилище данных
    const auto expectedData = fillTestJournal(journal);

    // Воспроизводим операции из журнала
    std::unordered_map<std::string, std::string> dataStore;
    EXPECT_TRUE(journal.replayJournal(dataStore));

    // Проверяем, что состояние хранилища соответствует ожидаемому
    EXPECT_EQ(dataStore.size(), expectedData.size());
    for (const auto &[uuid, data] : expectedData) {
        ASSERT_TRUE(dataStore.find(uuid) != dataStore.end());
        EXPECT_EQ(dataStore[uuid], data);
    }
}

// Тест проверки специальных символов в данных
TEST_F(JournalManagerTest, SpecialCharacters)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    // Создаем данные со специальными символами
    const std::string uuid = "test_uuid_special";
    const std::string specialData = "data with |special| chars\nand\rnewlines\\backslashes";

    // Записываем и проверяем
    EXPECT_TRUE(journal.writeInsert(uuid, specialData));

    // Создаем новое хранилище для воспроизведения
    std::unordered_map<std::string, std::string> dataStore;
    // Воспроизводим записанные действия и получаем заполненное хранилище
    EXPECT_TRUE(journal.replayJournal(dataStore));

    // Проверяем, что данные восстановлены корректно
    ASSERT_TRUE(dataStore.find(uuid) != dataStore.end());
    EXPECT_EQ(dataStore[uuid], specialData);
}

// Тест воспроизведения журнала с указанной контрольной точкой
TEST_F(JournalManagerTest, ReplayJournalFromCheckpoint)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    // Создаем первую партию данных
    std::unordered_map<std::string, std::string> expectedDataBefore;
    for (size_t i = 0; i < 5; i++) {
        const auto uuid = "uuid_before_" + std::to_string(i);
        const auto data = "data_before_" + std::to_string(i);
        EXPECT_TRUE(journal.writeInsert(uuid, data));
        expectedDataBefore[uuid] = data;
    }

    // Создаем контрольную точку
    const std::string checkpointId = "test_checkpoint";
    EXPECT_TRUE(journal.writeCheckpoint(checkpointId));

    // Создаем вторую партию данных
    std::unordered_map<std::string, std::string> expectedDataAfter;
    for (size_t i = 0; i < 5; i++) {
        const auto uuid = "uuid_after_" + std::to_string(i);
        const auto data = "data_after_" + std::to_string(i);
        EXPECT_TRUE(journal.writeInsert(uuid, data));
        expectedDataAfter[uuid] = data;
    }

    // Воспроизводим только операции после контрольной точки
    std::unordered_map<std::string, std::string> dataStore;
    EXPECT_TRUE(journal.replayJournal(dataStore, checkpointId));

    // Проверяем, что состояние хранилища содержит только данные после контрольной точки
    EXPECT_EQ(dataStore.size(), expectedDataAfter.size());
    for (const auto &[uuid, data] : expectedDataAfter) {
        ASSERT_TRUE(dataStore.find(uuid) != dataStore.end());
        EXPECT_EQ(dataStore[uuid], data);
    }

    // Проверяем, что данные до контрольной точки отсутствуют
    for (const auto &[uuid, _] : expectedDataBefore) {
        EXPECT_TRUE(dataStore.find(uuid) == dataStore.end());
    }
}

// Тест получения последней контрольной точки
TEST_F(JournalManagerTest, GetLastCheckpointId)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    // Изначально в журнале нет контрольных точек
    EXPECT_FALSE(journal.getLastCheckpointId().has_value());

    // Добавляем несколько контрольных точек
    const std::vector<std::string> checkpointIds
        = { "checkpoint_1", "checkpoint_2", "checkpoint_3" };

    for (const auto &id : checkpointIds) {
        // Добавляем операцию между контрольными точками
        const auto uuid = "uuid_" + id;
        const auto data = "data_" + id;
        EXPECT_TRUE(journal.writeInsert(uuid, data));

        // Добавляем контрольную точку
        EXPECT_TRUE(journal.writeCheckpoint(id));

        // Проверяем, что последняя контрольная точка обновлена
        auto lastCheckpoint = journal.getLastCheckpointId();
        ASSERT_TRUE(lastCheckpoint.has_value());
        EXPECT_EQ(*lastCheckpoint, id);
    }
}

// Тест подсчета операций после последней контрольной точки
TEST_F(JournalManagerTest, CountOperationsSinceLastCheckpoint)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    // Проверяем, что изначально операций нет
    auto count = journal.countOperationsSinceLastCheckpoint();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 0);

    // Добавляем контрольную точку в пустой журнал
    const std::string checkpoint1 = "checkpoint_1";
    EXPECT_TRUE(journal.writeCheckpoint(checkpoint1));

    // Проверяем, что подсчитана только одна операция - контрольная точка
    count = journal.countOperationsSinceLastCheckpoint();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 1);

    // Добавляем несколько операций
    constexpr size_t numOperations = 5;
    for (size_t i = 0; i < numOperations; i++) {
        const auto uuid = "uuid_" + std::to_string(i);
        const auto data = "data_" + std::to_string(i);
        EXPECT_TRUE(journal.writeInsert(uuid, data));
    }

    // Проверяем количество операций
    count = journal.countOperationsSinceLastCheckpoint();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, numOperations + 1); // (+ контрольная точка)

    // Добавляем новую контрольную точку
    const std::string checkpoint2 = "checkpoint_2";
    EXPECT_TRUE(journal.writeCheckpoint(checkpoint2));

    // Проверяем, что подсчитана только одна операция - контрольная точка
    count = journal.countOperationsSinceLastCheckpoint();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 1);
}

// Тест очистки журнала до определенной контрольной точки
TEST_F(JournalManagerTest, TruncateJournalToCheckpoint)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    // Создаем первую партию данных
    for (size_t i = 0; i < 5; i++) {
        const auto uuid = "uuid_before_" + std::to_string(i);
        const auto data = "data_before_" + std::to_string(i);
        EXPECT_TRUE(journal.writeInsert(uuid, data));
    }

    // Создаем первую контрольную точку
    const std::string checkpoint1 = "checkpoint_1";
    EXPECT_TRUE(journal.writeCheckpoint(checkpoint1));

    // Создаем вторую партию данных
    for (size_t i = 0; i < 3; i++) {
        const auto uuid = "uuid_middle_" + std::to_string(i);
        const auto data = "data_middle_" + std::to_string(i);
        EXPECT_TRUE(journal.writeInsert(uuid, data));
    }

    // Создаем вторую контрольную точку
    const std::string checkpoint2 = "checkpoint_2";
    EXPECT_TRUE(journal.writeCheckpoint(checkpoint2));

    // Создаем третью партию данных
    for (size_t i = 0; i < 2; i++) {
        const auto uuid = "uuid_after_" + std::to_string(i);
        const auto data = "data_after_" + std::to_string(i);
        EXPECT_TRUE(journal.writeInsert(uuid, data));
    }

    // Сохраняем исходный размер журнала
    const auto originalSize = std::filesystem::file_size(journalPath);

    // Очищаем журнал до второй контрольной точки
    EXPECT_TRUE(journal.truncateJournalToCheckpoint(checkpoint2));

    // Проверяем, что размер журнала уменьшился
    const auto newSize = std::filesystem::file_size(journalPath);
    EXPECT_LT(newSize, originalSize);

    // Проверяем, что операции до второй контрольной точки удалены,
    // а операции после контрольной точки остались
    EXPECT_FALSE(journalContains(journalPath, "uuid_before_"));
    EXPECT_FALSE(journalContains(journalPath, "uuid_middle_"));
    EXPECT_TRUE(journalContains(journalPath, "uuid_after_"));

    // Последняя контрольная точка должна быть сохранена
    auto lastCheckpoint = journal.getLastCheckpointId();
    ASSERT_TRUE(lastCheckpoint.has_value());
    EXPECT_EQ(*lastCheckpoint, checkpoint2);
}

// TODO: тест рабочий, но пока отключаем его, чтобы сильно не изнашивать диск
// Тест с очень большими данными
// TEST_F(JournalManagerTest, LargeData)
// {
//     const auto journalPath = getTestJournalPath();
//     JournalManager journal(journalPath);

//     // Создаем большую строку данных
//     const auto largeData = generateLargeString();
//     constexpr size_t INSERT_COUNT = 100;
//     for (size_t i = 0; i < INSERT_COUNT; i++) {
//         const std::string uuid = "large_data_uuid_" + std::to_string(i);
//         EXPECT_TRUE(journal.writeInsert(uuid, largeData));
//     }
//     // В итоге получаем файл размером 1 Гб

//     // Воспроизводим операции
//     std::unordered_map<std::string, std::string> dataStore;
//     EXPECT_TRUE(journal.replayJournal(dataStore));

//     // Проверяем, что большие данные восстановлены корректно
//     EXPECT_EQ(dataStore.size(), INSERT_COUNT);
//     for (size_t i = 0; i < INSERT_COUNT; i++) {
//         const std::string uuid = "large_data_uuid_" + std::to_string(i);
//         ASSERT_TRUE(dataStore.find(uuid) != dataStore.end());
//         EXPECT_EQ(dataStore[uuid], largeData);
//     }
// }

// Тест с пустыми данными
TEST_F(JournalManagerTest, EmptyData)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    // Записываем пустые данные
    const std::string uuid = "empty_data_uuid";
    EXPECT_TRUE(journal.writeInsert(uuid));

    // Воспроизводим операции
    std::unordered_map<std::string, std::string> dataStore;
    EXPECT_TRUE(journal.replayJournal(dataStore));

    // Проверяем, что пустые данные восстановлены корректно
    ASSERT_TRUE(dataStore.find(uuid) != dataStore.end());
    EXPECT_TRUE(dataStore[uuid].empty());
}

// Тест последовательности операций над одним UUID
TEST_F(JournalManagerTest, SequentialOperations)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    // Создаем последовательность операций над одним UUID

    const std::string uuid = "sequential_uuid";

    // INSERT
    {
        const std::string data = "initial_data";
        EXPECT_TRUE(journal.writeInsert(uuid, data));
        std::unordered_map<std::string, std::string> dataStore;
        EXPECT_TRUE(journal.replayJournal(dataStore));
        ASSERT_TRUE(dataStore.find(uuid) != dataStore.end());
        EXPECT_EQ(dataStore[uuid], data);
    }

    // UPDATE
    {
        const std::string updatedData = "updated_data";
        EXPECT_TRUE(journal.writeUpdate(uuid, updatedData));
        std::unordered_map<std::string, std::string> dataStore;
        EXPECT_TRUE(journal.replayJournal(dataStore));
        ASSERT_TRUE(dataStore.find(uuid) != dataStore.end());
        EXPECT_EQ(dataStore[uuid], updatedData);
    }

    // REMOVE
    EXPECT_TRUE(journal.writeRemove(uuid));
    {
        std::unordered_map<std::string, std::string> dataStore;
        EXPECT_TRUE(journal.replayJournal(dataStore));
        EXPECT_TRUE(dataStore.find(uuid) == dataStore.end());
    }

    // INSERT снова
    {
        const std::string newData = "new_data_after_remove";
        EXPECT_TRUE(journal.writeInsert(uuid, newData));
        std::unordered_map<std::string, std::string> dataStore;
        EXPECT_TRUE(journal.replayJournal(dataStore));
        ASSERT_TRUE(dataStore.find(uuid) != dataStore.end());
        EXPECT_EQ(dataStore[uuid], newData);
    }
}

// Тест непоследовательных операций (UPDATE/REMOVE для несуществующего UUID)
TEST_F(JournalManagerTest, NonSequentialOperations)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    const std::string uuid = "nonexistent_uuid";
    const std::string data = "data_for_nonexistent";

    // UPDATE для несуществующего UUID
    EXPECT_TRUE(journal.writeUpdate(uuid, data));

    // Воспроизводим операции - UPDATE для несуществующего UUID должен быть проигнорирован
    {
        std::unordered_map<std::string, std::string> dataStore;
        EXPECT_TRUE(journal.replayJournal(dataStore));
        EXPECT_TRUE(dataStore.find(uuid) == dataStore.end());
    }

    // REMOVE для несуществующего UUID
    EXPECT_TRUE(journal.writeRemove(uuid));

    // Воспроизводим операции - REMOVE для несуществующего UUID должен быть проигнорирован
    {
        std::unordered_map<std::string, std::string> dataStore;
        EXPECT_TRUE(journal.replayJournal(dataStore));
        EXPECT_TRUE(dataStore.find(uuid) == dataStore.end());
    }

    // INSERT для создания UUID
    EXPECT_TRUE(journal.writeInsert(uuid, data));

    // Воспроизводим операции - теперь UUID должен существовать
    {
        std::unordered_map<std::string, std::string> dataStore;
        EXPECT_TRUE(journal.replayJournal(dataStore));
        ASSERT_TRUE(dataStore.find(uuid) != dataStore.end());
        EXPECT_EQ(dataStore[uuid], data);
    }
}

// Тест с несколькими журналами
TEST_F(JournalManagerTest, MultipleJournals)
{
    const auto journalPath1 = getTestJournalPath(std::nullopt, "journal1.log");
    const auto journalPath2 = getTestJournalPath(std::nullopt, "journal2.log");

    // Создаем первый журнал
    JournalManager journal1(journalPath1);
    const std::string uuid1 = "uuid_journal1";
    const std::string data1 = "data_journal1";
    EXPECT_TRUE(journal1.writeInsert(uuid1, data1));

    // Создаем второй журнал
    JournalManager journal2(journalPath2);
    const std::string uuid2 = "uuid_journal2";
    const std::string data2 = "data_journal2";
    EXPECT_TRUE(journal2.writeInsert(uuid2, data2));

    // Проверяем, что оба журнала существуют и содержат нужные данные
    EXPECT_TRUE(std::filesystem::exists(journalPath1));
    EXPECT_TRUE(std::filesystem::exists(journalPath2));
    EXPECT_TRUE(journalContains(journalPath1, uuid1));
    EXPECT_TRUE(journalContains(journalPath2, uuid2));
    EXPECT_FALSE(journalContains(journalPath1, uuid2));
    EXPECT_FALSE(journalContains(journalPath2, uuid1));
}

// Стресс-тест параллельной записи в журнал
TEST_F(JournalManagerTest, ConcurrentWrite)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    constexpr size_t THREAD_COUNT = 20;
    constexpr size_t OPERATIONS_PER_THREAD = 100;

    // Запускаем несколько потоков для параллельной записи
    std::vector<std::future<bool>> futures;
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        futures.push_back(std::async(std::launch::async, [this, &journal, i]() {
            bool result = true;
            for (size_t j = 0; j < OPERATIONS_PER_THREAD; j++) {
                const auto uuid = "uuid_thread_" + std::to_string(i) + "_op_" + std::to_string(j);
                const auto data = "data_thread_" + std::to_string(i) + "_op_" + std::to_string(j);
                result &= journal.writeInsert(uuid, data);

                // Добавляем случайную задержку
                std::this_thread::sleep_for(std::chrono::milliseconds(getRandomInt(1, 5)));
            }
            return result;
        }));
    }

    // Все потоки должны завершиться успешно
    bool allSucceeded = true;
    for (auto &future : futures) {
        allSucceeded &= future.get();
    }
    EXPECT_TRUE(allSucceeded);

    // Проверяем, что все записи были сделаны
    std::unordered_map<std::string, std::string> dataStore;
    EXPECT_TRUE(journal.replayJournal(dataStore));
    EXPECT_EQ(dataStore.size(), THREAD_COUNT * OPERATIONS_PER_THREAD);
}

// Тест параллельной записи и чтения журнала
TEST_F(JournalManagerTest, ConcurrentWriteAndRead)
{
    const auto journalPath = getTestJournalPath();
    JournalManager journal(journalPath);

    constexpr size_t WRITER_THREADS = 10;
    constexpr size_t READER_THREADS = 5;
    constexpr size_t OPERATIONS_PER_THREAD = 20;

    std::atomic<bool> waitingStart(true);

    // Запускаем потоки для записи
    std::vector<std::future<size_t>> writerFutures;
    for (size_t i = 0; i < WRITER_THREADS; i++) {
        writerFutures.push_back(std::async(std::launch::async, [this, &journal, i,
                                                                &waitingStart]() {
            // Ждем разрешения начать работу
            while (waitingStart.load()) {
            }

            size_t successCount = 0;
            for (size_t j = 0; j < OPERATIONS_PER_THREAD; j++) {
                const auto uuid = "uuid_writer_" + std::to_string(i) + "_op_" + std::to_string(j);
                const auto data = "data_writer_" + std::to_string(i) + "_op_" + std::to_string(j);
                if (journal.writeInsert(uuid, data)) {
                    successCount++;
                }

                // Добавляем случайную задержку
                std::this_thread::sleep_for(std::chrono::milliseconds(getRandomInt(1, 10)));
            }
            return successCount;
        }));
    }

    // Запускаем потоки для чтения
    std::vector<std::future<size_t>> readerFutures;
    for (size_t i = 0; i < READER_THREADS; i++) {
        readerFutures.push_back(std::async(std::launch::async, [&journal, &waitingStart]() {
            // Ждем разрешения начать работу
            while (waitingStart.load()) {
            }

            size_t readCount = 0;
            std::unordered_map<std::string, std::string> dataStore;
            for (size_t j = 0; j < OPERATIONS_PER_THREAD; j++) {
                if (journal.replayJournal(dataStore)) {
                    readCount++;
                }

                // Добавляем случайную задержку
                std::this_thread::sleep_for(std::chrono::milliseconds(getRandomInt(5, 15)));
            }
            return readCount;
        }));
    }

    // Начинаем одновременную работу писателей и читателей
    waitingStart.store(false);

    // Ждем завершения писателей
    size_t totalWrites = 0;
    for (auto &future : writerFutures) {
        totalWrites += future.get();
    }

    // Ждем завершения читателей
    size_t totalReaders = 0;
    for (auto &future : readerFutures) {
        totalReaders += future.get();
    }

    // Проверяем, что все операции успешно завершились
    EXPECT_EQ(totalWrites, WRITER_THREADS * OPERATIONS_PER_THREAD);
    EXPECT_EQ(totalReaders, READER_THREADS * OPERATIONS_PER_THREAD);
}

// Тест восстановления после сбоя
TEST_F(JournalManagerTest, RecoveryAfterCrash)
{
    const auto journalPath = getTestJournalPath();

    auto generateDataBeforeCrash
        = [this](std::unordered_map<std::string, std::string> &dataStorage) {
              for (size_t i = 0; i < 10; i++) {
                  const auto uuid = "uuid_" + std::to_string(i);
                  const auto data = "data_" + std::to_string(i);
                  dataStorage[uuid] = data;
              }
          };

    const std::string checkpointId = "checkpoint_before_crash";
    std::unordered_map<std::string, std::string> expectedData;

    // Создаем журнал и добавляем данные
    {
        JournalManager journal(journalPath);

        // Добавляем операции
        for (size_t i = 0; i < 10; i++) {
            const auto uuid = "uuid_" + std::to_string(i);
            const auto data = "data_" + std::to_string(i);
            EXPECT_TRUE(journal.writeInsert(uuid, data));
            // Данные в expectedData заносим отдельно
        }
        generateDataBeforeCrash(expectedData);

        // Добавляем контрольную точку
        EXPECT_TRUE(journal.writeCheckpoint(checkpointId));

        // Обновляем половину записей
        for (size_t i = 0; i < 5; i++) {
            const auto uuid = "uuid_" + std::to_string(i);
            const auto data = "updated_data_" + std::to_string(i);
            EXPECT_TRUE(journal.writeUpdate(uuid, data));
            expectedData[uuid] = data;
        }

        // Удаляем часть записей
        for (size_t i = 5; i < 8; i++) {
            const auto uuid = "uuid_" + std::to_string(i);
            EXPECT_TRUE(journal.writeRemove(uuid));
            expectedData.erase(uuid);
        }
    }

    // Имитируем сбой системы и потерю всех данных
    {
        JournalManager journal(journalPath);

        std::unordered_map<std::string, std::string> dataAfterTotalCrash;
        EXPECT_TRUE(journal.replayJournal(dataAfterTotalCrash));

        EXPECT_EQ(dataAfterTotalCrash.size(), expectedData.size());
        for (const auto &[uuid, data] : expectedData) {
            ASSERT_TRUE(dataAfterTotalCrash.find(uuid) != dataAfterTotalCrash.end());
            EXPECT_EQ(dataAfterTotalCrash[uuid], data);
        }
    }

    // Имитируем сбой системы и потерю части данных (после контрольной точки)
    {
        JournalManager journal(journalPath);

        const auto lastCheckpointId = journal.getLastCheckpointId();
        ASSERT_TRUE(lastCheckpointId.has_value());
        EXPECT_EQ(*lastCheckpointId, checkpointId);

        std::unordered_map<std::string, std::string> dataAfterPartlyCrash;
        generateDataBeforeCrash(dataAfterPartlyCrash);
        EXPECT_TRUE(journal.replayJournal(dataAfterPartlyCrash, *lastCheckpointId));

        EXPECT_EQ(dataAfterPartlyCrash.size(), expectedData.size());
        for (const auto &[uuid, data] : expectedData) {
            ASSERT_TRUE(dataAfterPartlyCrash.find(uuid) != dataAfterPartlyCrash.end());
            EXPECT_EQ(dataAfterPartlyCrash[uuid], data);
        }
    }
}
} // namespace octet::tests
