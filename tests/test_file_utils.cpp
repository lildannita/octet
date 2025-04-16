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

namespace {
static constexpr size_t CONCURRENT_THREADS = 10;
static constexpr size_t CONCURRENT_OPERATIONS = 20;
static constexpr size_t LARGE_FILE_SIZE = 10 * 1024 * 1024; // 10 МБ
static constexpr size_t THREAD_START_TIMEOUT_MS = 100;
} // namespace

namespace octet::tests {
class FileUtilsTest : public ::testing::Test {
protected:
    std::filesystem::path testDir; // Путь к тестовой директории

    void SetUp() override
    {
        // Создаем временную директорию для тестов
        testDir = createTmpDirectory("FileUtils");

        // Включаем логгер для отладки
        // utils::Logger::getInstance().enable(true, std::nullopt, utils::LogLevel::DEBUG);
    }

    void TearDown() override
    {
        // Удаляем временную директорию
        removeTmpDirectory(testDir);
    }

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
     * @brief Создаёт тестовый файл с указанным содержимым
     * @param path Путь, где создать файл
     * @param content Содержимое для записи в файл
     */
    void createTestFile(const std::filesystem::path &path,
                        const std::string &content = "test content")
    {
        std::ofstream file(path, std::ios::binary);
        file.write(content.data(), content.size());
        file.close();
        EXPECT_TRUE(std::filesystem::exists(path));
    }

    /**
     * @brief Создаёт тестовую директорию по указанному пути
     * @param path Путь, где создать директорию
     */
    void createTestDir(const std::filesystem::path &path)
    {
        std::filesystem::create_directory(path);
        EXPECT_TRUE(std::filesystem::exists(path));
        EXPECT_TRUE(std::filesystem::is_directory(path));
    }

    /**
     * @brief Читает содержимое из файла
     * @param path Путь к файлу
     * @return Содержимое файла
     */
    std::string readFileContent(const std::filesystem::path &path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        EXPECT_TRUE(file.is_open());

        auto size = file.tellg();
        std::string content(static_cast<size_t>(size), '\0');
        file.seekg(0);
        file.read(&content[0], size);
        file.close();

        return content;
    }

    /**
     * @brief Генерирует строку указанного размера
     * @param size Размер генерируемой строки
     * @return Сгенерированная строка
     */
    static std::string generateLargeString(size_t size = LARGE_FILE_SIZE)
    {
        std::string result(size, 'X');
        for (size_t i = 0; i < size; i += 1024) {
            size_t pos = i % 26;
            result[i] = static_cast<char>('A' + pos);
        }
        return result;
    }

    /**
     * @brief Создаёт бинарную строку со всеми возможными значениями байтов
     * @return Строка, содержащая все значения байтов от 0 до 255
     */
    static std::string createBinaryContent()
    {
        std::string content;
        content.reserve(256);
        for (int i = 0; i < 256; i++) {
            content.push_back(static_cast<char>(i));
        }
        return content;
    }
};

// Тест проверки существования файла в существующей директории
TEST_F(FileUtilsTest, СheckIfFileExistsInDirExists)
{
    const auto filePath = getTestFilePath();
    EXPECT_FALSE(std::filesystem::exists(filePath));
    EXPECT_FALSE(utils::checkIfFileExists(filePath));

    createTestFile(filePath);
    EXPECT_TRUE(std::filesystem::exists(filePath));
    EXPECT_TRUE(utils::checkIfFileExists(filePath));
}

// Тест проверки существования файла в несуществующей директории
TEST_F(FileUtilsTest, СheckIfFileExistsInDirNonExists)
{
    const auto dirPath = testDir / "new_dir";
    const auto filePath = dirPath / "test_file.txt";

    EXPECT_FALSE(std::filesystem::exists(dirPath));
    EXPECT_FALSE(std::filesystem::exists(filePath));

    // Проверяем без создания директории
    EXPECT_FALSE(utils::checkIfFileExists(filePath, false));

    // После проверки директория так же должна не существовать
    EXPECT_FALSE(std::filesystem::exists(dirPath));

    // Проверяем с созданием директории, но файл так же должен не существовать
    EXPECT_FALSE(utils::checkIfFileExists(filePath));
    EXPECT_TRUE(std::filesystem::exists(dirPath));

    createTestFile(filePath);
    EXPECT_TRUE(std::filesystem::exists(filePath));
    EXPECT_TRUE(utils::checkIfFileExists(filePath));
}

// Проверка существования несуществующей директории
TEST_F(FileUtilsTest, EnsureDirectoryNonExists)
{
    const auto dirPath = testDir / "new_dir";
    EXPECT_FALSE(std::filesystem::exists(dirPath));

    EXPECT_FALSE(utils::ensureDirectoryExists(dirPath, false));
    EXPECT_TRUE(utils::ensureDirectoryExists(dirPath));
    EXPECT_TRUE(std::filesystem::exists(dirPath));
    EXPECT_TRUE(std::filesystem::is_directory(dirPath));
}

// Проверка существующей директорией
TEST_F(FileUtilsTest, EnsureDirectoryAlreadyExists)
{
    auto dirPath = testDir / "existing_dir";
    createTestDir(dirPath);
    EXPECT_TRUE(utils::ensureDirectoryExists(dirPath));
    EXPECT_TRUE(std::filesystem::is_directory(dirPath));
}

// Проверка работы с вложенными директориями
TEST_F(FileUtilsTest, EnsureNestedDirsExistance)
{
    const auto level1 = testDir / "level1";
    const auto level2 = level1 / "level2";
    const auto dirPath = level2 / "level3";
    EXPECT_FALSE(std::filesystem::exists(level1));
    EXPECT_FALSE(std::filesystem::exists(level2));
    EXPECT_FALSE(std::filesystem::exists(dirPath));

    EXPECT_FALSE(utils::ensureDirectoryExists(dirPath, false));
    EXPECT_TRUE(utils::ensureDirectoryExists(dirPath));
    EXPECT_TRUE(std::filesystem::exists(level1));
    EXPECT_TRUE(std::filesystem::is_directory(level1));
    EXPECT_TRUE(std::filesystem::exists(level2));
    EXPECT_TRUE(std::filesystem::is_directory(level2));
    EXPECT_TRUE(std::filesystem::exists(dirPath));
    EXPECT_TRUE(std::filesystem::is_directory(dirPath));
}

// Тест проверки директории с путём к файлу
TEST_F(FileUtilsTest, EnsureDirectoryExistsWithFile)
{
    const auto filePath = getTestFilePath();
    createTestFile(filePath);
    EXPECT_FALSE(std::filesystem::is_directory(filePath));
    EXPECT_FALSE(utils::ensureDirectoryExists(filePath));
}

// Тест с директорией с ограниченным доступом
TEST_F(FileUtilsTest, EnsureRestrictedAccessDirectoryExists)
{
#if defined(OCTET_PLATFORM_UNIX)
    const auto dirPath = testDir / "restricted_dir";
    createTestDir(dirPath);
    EXPECT_TRUE(utils::ensureDirectoryExists(dirPath, false));

    // Устанавливаем директорию только для чтения
    std::filesystem::permissions(dirPath,
                                 std::filesystem::perms::owner_read
                                     | std::filesystem::perms::group_read
                                     | std::filesystem::perms::others_read,
                                 std::filesystem::perm_options::replace);
    EXPECT_TRUE(utils::ensureDirectoryExists(dirPath, false));

    // Тест с созданием поддиректории в директории только для чтения
    const auto subDirPath = dirPath / "subdir";
    EXPECT_FALSE(utils::ensureDirectoryExists(subDirPath));

    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(subDirPath, ec));
    EXPECT_TRUE(ec);

    // Восстанавливаем права для очистки
    std::filesystem::permissions(dirPath, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
#endif
}

// Тест с параллельным созданием директории
TEST_F(FileUtilsTest, EnsureDirectoryExistsConcurrent)
{
    // constexpr size_t THREAD_COUNT = 20;
    constexpr size_t THREAD_COUNT = 5;
    std::vector<std::future<bool>> futures;

    for (size_t i = 0; i < THREAD_COUNT; i++) {
        futures.push_back(std::async(std::launch::async, [this, i]() {
            const auto dirPath = testDir / "concurrent_dir";
            const auto result = utils::ensureDirectoryExists(dirPath);
            EXPECT_TRUE(result);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return result;
        }));
    }

    // Все потоки должны успешно завершиться
    size_t successCount = 0;
    for (auto &future : futures) {
        if (future.get()) {
            successCount++;
        }
    }

    EXPECT_EQ(THREAD_COUNT, successCount);
    EXPECT_TRUE(std::filesystem::exists(testDir / "concurrent_dir"));
}

// Тест базовой атомарной записи в несуществующий файл
TEST_F(FileUtilsTest, AtomicFileWriteBasic)
{
    const auto filePath = getTestFilePath();
    const std::string content = "Это тестовое содержимое для атомарной записи";

    EXPECT_FALSE(std::filesystem::exists(filePath));
    EXPECT_TRUE(utils::atomicFileWrite(filePath, content));
    EXPECT_TRUE(std::filesystem::exists(filePath));

    // Проверяем содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(content, readContent);
}

// Тест атомарной записи в существующий файл
TEST_F(FileUtilsTest, AtomicFileWriteOverwrite)
{
    const auto filePath = getTestFilePath();
    const std::string initialContent = "Начальное содержимое";
    const std::string newContent = "Новое содержимое для теста перезаписи";

    EXPECT_FALSE(std::filesystem::exists(filePath));
    createTestFile(filePath, initialContent);
    EXPECT_TRUE(utils::atomicFileWrite(filePath, newContent));

    // Проверяем новое содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(newContent, readContent);
}

// Тест атомарной записи с пустым содержимым
TEST_F(FileUtilsTest, AtomicFileWriteEmpty)
{
    const auto filePath = getTestFilePath();
    const std::string content = "";

    EXPECT_FALSE(std::filesystem::exists(filePath));
    EXPECT_TRUE(utils::atomicFileWrite(filePath, content));
    EXPECT_TRUE(std::filesystem::exists(filePath));

    // Проверяем пустое содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(content, readContent);
}

// Тест атомарной записи с большим содержимым
TEST_F(FileUtilsTest, AtomicFileWriteLarge)
{
    const auto filePath = getTestFilePath();
    // Создаем строку большим размером
    const auto largeContent = generateLargeString();

    EXPECT_FALSE(std::filesystem::exists(filePath));
    EXPECT_TRUE(utils::atomicFileWrite(filePath, largeContent));
    EXPECT_TRUE(std::filesystem::exists(filePath));

    // Проверяем большое содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(largeContent, readContent);
}

// Тест атомарной записи с бинарным содержимым
TEST_F(FileUtilsTest, AtomicFileWriteBinary)
{
    const auto filePath = getTestFilePath();
    const auto binaryContent = createBinaryContent();

    EXPECT_FALSE(std::filesystem::exists(filePath));
    EXPECT_TRUE(utils::atomicFileWrite(filePath, binaryContent));
    EXPECT_TRUE(std::filesystem::exists(filePath));

    // Проверяем бинарное содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(binaryContent, readContent);
}

// Тест атомарной записи в директорию
TEST_F(FileUtilsTest, AtomicFileWriteToDirectory)
{
    const auto dirPath = testDir / "test_dir";
    createTestDir(dirPath);
    EXPECT_TRUE(utils::ensureDirectoryExists(dirPath, false));

    const std::string content = "Это должно вызвать ошибку";
    EXPECT_FALSE(utils::atomicFileWrite(dirPath, content));
}

// Тест атомарной записи в файл в несуществующей директории
TEST_F(FileUtilsTest, AtomicFileWriteToNonExistentDirectory)
{
    const auto dirPath = testDir / "nonexistent_dir";
    const auto filePath = dirPath / "test_file.txt";
    const std::string content = "Тестовое содержимое для несуществующей директории";

    EXPECT_FALSE(std::filesystem::exists(dirPath));
    EXPECT_FALSE(std::filesystem::exists(filePath));
    EXPECT_TRUE(utils::atomicFileWrite(filePath, content));
    EXPECT_TRUE(std::filesystem::exists(dirPath));
    EXPECT_TRUE(std::filesystem::exists(filePath));

    // Проверяем содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(content, readContent);
}

// Тест параллельных атомарных записей в один и тот же файл
TEST_F(FileUtilsTest, AtomicFileWriteConcurrent)
{
    const auto filePath = getTestFilePath();
    auto generateContent = [](size_t index) {
        return std::string("Содержимое от писателя " + std::to_string(index));
    };

    // Запускаем несколько потоков-писателей
    constexpr size_t THREAD_COUNT = 30;
    std::vector<std::future<bool>> writers;

    for (size_t i = 0; i < THREAD_COUNT; i++) {
        writers.push_back(std::async(std::launch::async, [&filePath, i, &generateContent]() {
            return utils::atomicFileWrite(filePath, generateContent(i));
        }));
    }

    // Все потоки должны успешно завершиться
    size_t successCount = 0;
    for (auto &future : writers) {
        if (future.get()) {
            successCount++;
        }
    }
    EXPECT_EQ(THREAD_COUNT, successCount);

    // Проверяем, что файл существует
    EXPECT_TRUE(std::filesystem::exists(filePath));

    // Содержимое должно быть от одного из писателей
    const auto readContent = readFileContent(filePath);
    EXPECT_FALSE(readContent.empty());
    bool foundMatch = false;
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        if (readContent == generateContent(i)) {
            foundMatch = true;
            break;
        }
    }
    EXPECT_TRUE(foundMatch);
}

// Тест атомарной записи с параллельными читателями
TEST_F(FileUtilsTest, AtomicFileWriteWithConcurrentReaders)
{
    const auto filePath = getTestFilePath();
    const std::string initialContent = "Начальное содержимое для теста с параллельными операциями";
    const std::string newContent = "Новое содержимое для теста с параллельными операциями";
    createTestFile(filePath, initialContent);

    constexpr size_t THREAD_COUNT = 10;
    std::vector<std::future<bool>> readers;
    std::atomic<bool> keepReading(true);

    // Запускаем несколько потоков-читателей
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        readers.push_back(std::async(std::launch::async, [&filePath, &keepReading]() {
            std::string content;
            while (keepReading.load()) {
                utils::safeFileRead(filePath, content);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return true;
        }));
    }

    // Даем читателям время начать работу
    std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_START_TIMEOUT_MS));

    // Выполняем атомарную запись
    EXPECT_TRUE(utils::atomicFileWrite(filePath, newContent));

    // Останавливаем читателей
    keepReading.store(false);
    for (auto &future : readers) {
        future.wait();
    }

    // Проверяем содержимое
    std::string readContent;
    EXPECT_TRUE(utils::safeFileRead(filePath, readContent));
    EXPECT_EQ(newContent, readContent);
    readContent = readFileContent(filePath);
    EXPECT_EQ(newContent, readContent);
}

// Тест базового чтения из существующего файла
TEST_F(FileUtilsTest, SafeFileReadBasic)
{
    const auto filePath = getTestFilePath();
    const std::string content = "Содержимое для теста безопасного чтения";
    createTestFile(filePath, content);

    std::string readContent;
    EXPECT_TRUE(utils::safeFileRead(filePath, readContent));
    EXPECT_EQ(content, readContent);
}

// Тест чтения из несуществующего файла
TEST_F(FileUtilsTest, SafeFileReadNonExistent)
{
    const auto filePath = getTestFilePath();
    EXPECT_FALSE(std::filesystem::exists(filePath));

    std::string readContent;
    EXPECT_FALSE(utils::safeFileRead(filePath, readContent));
    EXPECT_TRUE(readContent.empty());
}

// Тест чтения из пустого файла
TEST_F(FileUtilsTest, SafeFileReadEmpty)
{
    const auto filePath = getTestFilePath();
    createTestFile(filePath, "");

    std::string readContent = "не пусто";
    EXPECT_TRUE(utils::safeFileRead(filePath, readContent));
    EXPECT_TRUE(readContent.empty());
}

// Тест чтения из большого файла
TEST_F(FileUtilsTest, SafeFileReadLarge)
{
    const auto filePath = getTestFilePath();
    const auto largeContent = generateLargeString();
    createTestFile(filePath, largeContent);

    std::string readContent;
    EXPECT_TRUE(utils::safeFileRead(filePath, readContent));
    EXPECT_EQ(largeContent, readContent);
}

// Тест чтения из бинарного файла
TEST_F(FileUtilsTest, SafeFileReadBinary)
{
    const auto filePath = getTestFilePath();
    const auto binaryContent = createBinaryContent();

    // Записываем в бинарном режиме
    {
        std::ofstream file(filePath, std::ios::binary);
        file.write(binaryContent.data(), binaryContent.size());
    }
    EXPECT_TRUE(std::filesystem::exists(filePath));

    std::string readContent;
    EXPECT_TRUE(utils::safeFileRead(filePath, readContent));
    EXPECT_EQ(binaryContent, readContent);
}

// Тест чтения из директории
TEST_F(FileUtilsTest, SafeFileReadFromDirectory)
{
    const auto dirPath = testDir / "test_dir";
    createTestDir(dirPath);

    std::string readContent;
    EXPECT_FALSE(utils::safeFileRead(dirPath, readContent));
}

// Тест параллельного чтения из одного файла
TEST_F(FileUtilsTest, SafeFileReadConcurrent)
{
    const auto filePath = getTestFilePath();
    const std::string content = "Содержимое для теста параллельного чтения";
    createTestFile(filePath, content);

    constexpr size_t THREAD_COUNT = 20;
    std::vector<std::future<std::string>> readers;

    for (size_t i = 0; i < THREAD_COUNT; i++) {
        readers.push_back(std::async(std::launch::async, [&filePath]() {
            std::string readContent;
            EXPECT_TRUE(utils::safeFileRead(filePath, readContent));
            return readContent;
        }));
    }

    // Проверяем, что все чтения успешны и вернули правильное содержимое
    for (auto &future : readers) {
        const auto result = future.get();
        EXPECT_EQ(content, result);
    }
}

// Тест чтения с параллельными писателями
TEST_F(FileUtilsTest, SafeFileReadWithConcurrentWriters)
{
    const auto filePath = getTestFilePath();
    const std::string initialContent
        = "Начальное содержимое для теста параллельного чтения/записи ";
    createTestFile(filePath, initialContent);

    auto generateContent = [](size_t index) {
        return std::string("Обновление содержимого " + std::to_string(index));
    };

    // Запускаем несколько операций чтения, пока другой поток постоянно пишет
    constexpr size_t THREAD_COUNT = 50;
    std::atomic<bool> keepWriting(true);
    size_t writeCounter = 0;

    // Запускаем поток-писатель
    auto writerFuture = std::async(
        std::launch::async, [&filePath, &keepWriting, &generateContent, &writeCounter]() {
            while (keepWriting.load()) {
                EXPECT_TRUE(utils::atomicFileWrite(filePath, generateContent(writeCounter++)));
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });

    // Даем писателю время начать работу
    std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_START_TIMEOUT_MS));

    // Выполняем параллельные чтения
    std::vector<std::future<std::string>> readers;
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        readers.push_back(std::async(std::launch::async, [&filePath]() {
            std::string content;
            EXPECT_TRUE(utils::safeFileRead(filePath, content));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return content;
        }));
    }

    // Даем читателям время начать работу и поработать в режиме конкуренции
    std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_START_TIMEOUT_MS));

    // Останавливаем писателя
    keepWriting.store(false);
    writerFuture.wait();

    // Получаем результаты чтения, которые должны совпадать хотя бы с одним из вариантов записи
    for (auto &future : readers) {
        bool found = false;
        size_t counter = 0;
        const auto readContent = future.get();
        while (counter <= writeCounter) {
            if (generateContent(counter++) == readContent) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }
}

// Тест чтения из файла без прав на чтение
TEST_F(FileUtilsTest, SafeFileReadNoPermission)
{
#if defined(OCTET_PLATFORM_UNIX)
    const auto filePath = getTestFilePath();
    const std::string content = "Это тестовый файл без прав на чтение";
    createTestFile(filePath, content);

    // Убираем права на чтение
    std::filesystem::permissions(filePath, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);

    std::string readContent;
    EXPECT_FALSE(utils::safeFileRead(filePath, readContent));
    EXPECT_TRUE(readContent.empty());

    // Восстанавливаем права для очистки
    std::filesystem::permissions(filePath, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
#endif
}

// Тест с читаемым файлом
TEST_F(FileUtilsTest, IsFileReadableBasic)
{
    const auto filePath = getTestFilePath();
    createTestFile(filePath, "Тестовое содержимое");
    EXPECT_TRUE(utils::isFileReadable(filePath));
}

// Тест с несуществующим файлом
TEST_F(FileUtilsTest, IsFileReadableNonExistent)
{
    const auto filePath = getTestFilePath();
    EXPECT_FALSE(std::filesystem::exists(filePath));
    EXPECT_FALSE(utils::isFileReadable(filePath));
}

// Тест с пустым файлом
TEST_F(FileUtilsTest, IsFileReadableEmpty)
{
    const auto filePath = getTestFilePath();
    createTestFile(filePath, "");
    EXPECT_TRUE(utils::isFileReadable(filePath));
}

// Тест с директорией
TEST_F(FileUtilsTest, IsFileReadableDirectory)
{
    const auto dirPath = testDir / "test_dir";
    createTestDir(dirPath);
    EXPECT_FALSE(utils::isFileReadable(dirPath));
}

// Тест с файлом без прав на чтение
TEST_F(FileUtilsTest, IsFileReadableNoPermission)
{
#if defined(OCTET_PLATFORM_UNIX)
    const auto filePath = getTestFilePath();
    createTestFile(filePath, "Тестовое содержимое");

    // Убираем права на чтение
    std::filesystem::permissions(filePath, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);

    EXPECT_FALSE(utils::isFileReadable(filePath));

    // Восстанавливаем права для очистки
    std::filesystem::permissions(filePath, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
#endif
}

// Тест с символической ссылкой
TEST_F(FileUtilsTest, IsFileReadableSymlink)
{
    const auto filePath = getTestFilePath();
    const auto linkPath = testDir / "link_to_file.txt";
    createTestFile(filePath, "Тестовое содержимое");

    try {
        std::filesystem::create_symlink(filePath, linkPath);
        EXPECT_TRUE(std::filesystem::exists(linkPath));
        EXPECT_TRUE(std::filesystem::is_symlink(linkPath));
        EXPECT_TRUE(utils::isFileReadable(linkPath));
    }
    catch (const std::filesystem::filesystem_error &e) {
        // Создание символических ссылок может требовать повышенных привилегий в некоторыхсистемах
        std::cout << "Пропуск теста со ссылкой из-за ошибки: " << e.what() << std::endl;
    }
}

// Тест с параллельными проверками
TEST_F(FileUtilsTest, IsFileReadableConcurrent)
{
    const auto filePath = getTestFilePath();
    createTestFile(filePath, "Тестовое содержимое");

    constexpr size_t THREAD_COUNT = 20;
    std::vector<std::future<bool>> futures;

    for (size_t i = 0; i < THREAD_COUNT; i++) {
        futures.push_back(std::async(std::launch::async, [&filePath]() {
            const auto readable = utils::isFileReadable(filePath);
            EXPECT_TRUE(readable);
            return readable;
        }));
    }

    // Все потоки должны сообщить о читаемости файла
    size_t successCount = 0;
    for (auto &future : futures) {
        if (future.get()) {
            successCount++;
        }
    }
    EXPECT_EQ(successCount, THREAD_COUNT);
}

// Тест базового добавления к существующему файлу
TEST_F(FileUtilsTest, SafeFileAppendBasic)
{
    const auto filePath = getTestFilePath();
    const std::string initialContent = "Начальное содержимое";
    const std::string appendContent = " - добавленное содержимое";
    createTestFile(filePath, initialContent);

    EXPECT_TRUE(utils::safeFileAppend(filePath, appendContent));

    // Проверяем объединенное содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(initialContent + appendContent, readContent);
}

// Тест добавления к несуществующему файлу
TEST_F(FileUtilsTest, SafeFileAppendNonExistent)
{
    const auto filePath = getTestFilePath();
    const std::string content = "Содержимое для добавления в несуществующий файл";
    EXPECT_FALSE(std::filesystem::exists(filePath));

    EXPECT_TRUE(utils::safeFileAppend(filePath, content));
    EXPECT_TRUE(std::filesystem::exists(filePath));

    // Проверяем содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(content, readContent);
}

// Тест добавления пустой строки
TEST_F(FileUtilsTest, SafeFileAppendEmpty)
{
    const auto filePath = getTestFilePath();
    const std::string content = "Начальное содержимое";
    createTestFile(filePath, content);

    EXPECT_TRUE(utils::safeFileAppend(filePath, ""));

    // Проверяем, что содержимое не изменилось
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(content, readContent);
}

// Тест множественных добавлений
TEST_F(FileUtilsTest, SafeFileAppendMultiple)
{
    const auto filePath = getTestFilePath();
    const std::string initialContent = "Начальное содержимое";
    createTestFile(filePath, initialContent);

    const std::vector<std::string> appendContents
        = { " - первое добавление", " - второе добавление", " - третье добавление" };

    std::string expectedContent = initialContent;
    for (const auto &appendContent : appendContents) {
        EXPECT_TRUE(utils::safeFileAppend(filePath, appendContent));
        expectedContent += appendContent;
    }

    // Проверяем итоговое содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(expectedContent, readContent);
}

// Тест добавления к директории
TEST_F(FileUtilsTest, SafeFileAppendToDirectory)
{
    const auto dirPath = testDir / "test_dir";
    createTestDir(dirPath);
    EXPECT_FALSE(utils::safeFileAppend(dirPath, "Это должно вызвать ошибку"));
}

// Тест добавления к файлу в несуществующей директории
TEST_F(FileUtilsTest, SafeFileAppendToNonExistentDirectory)
{
    const auto dirPath = testDir / "nonexistent_dir";
    const auto filePath = dirPath / "test_file.txt";
    const std::string content = "Содержимое для добавления к файлу в несуществующей директории";

    EXPECT_FALSE(std::filesystem::exists(dirPath));
    EXPECT_TRUE(utils::safeFileAppend(filePath, content));
    EXPECT_TRUE(std::filesystem::exists(dirPath));
    EXPECT_TRUE(std::filesystem::is_directory(dirPath));
    EXPECT_TRUE(std::filesystem::exists(filePath));

    // Проверяем содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(content, readContent);
}

// Тест добавления с параллельными читателями
TEST_F(FileUtilsTest, SafeFileAppendWithConcurrentReaders)
{
    const auto filePath = getTestFilePath();
    const std::string initialContent = "Начальное содержимое для теста с параллельными операциями";
    createTestFile(filePath, initialContent);

    // Запускаем несколько потоков-читателей
    constexpr size_t THREAD_COUNT = 20;
    std::vector<std::future<std::string>> readers;
    std::atomic<bool> keepReading(true);

    for (size_t i = 0; i < THREAD_COUNT; i++) {
        readers.push_back(std::async(std::launch::async, [&filePath, &keepReading]() {
            std::string content;
            while (keepReading.load()) {
                EXPECT_TRUE(utils::safeFileRead(filePath, content));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return content;
        }));
    }

    // Даем читателям время начать работу
    std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_START_TIMEOUT_MS));

    // Выполняем добавление
    const std::string appendContent = " - добавлено с параллельными читателями";
    EXPECT_TRUE(utils::safeFileAppend(filePath, appendContent));

    // Даем читателям время, чтобы прочитать измененный файл
    std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_START_TIMEOUT_MS));

    // Останавливаем читателей и проверяем результат чтения
    keepReading.store(false);
    for (auto &future : readers) {
        EXPECT_EQ(future.get(), initialContent + appendContent);
    }

    // Проверяем содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(initialContent + appendContent, readContent);
}

// Тест параллельных добавлений
TEST_F(FileUtilsTest, SafeFileAppendConcurrent)
{
    const auto filePath = getTestFilePath();
    const std::string initialContent = "Начальное содержимое";
    createTestFile(filePath, initialContent);

    auto generateThreadMarker
        = [](size_t index) { return std::string("[Поток " + std::to_string(index)); };
    auto generateAppendMarker
        = [](size_t index) { return std::string(" Добавление " + std::to_string(index) + "]"); };

    constexpr size_t THREAD_COUNT = 10;
    constexpr size_t APPENDS_PER_THREAD = 10;

    // Каждый поток будет добавлять свой идентификатор несколько раз
    std::vector<std::future<void>> appenders;
    std::atomic<size_t> successAppends = 0;

    for (size_t i = 0; i < THREAD_COUNT; i++) {
        appenders.push_back(std::async(std::launch::async, [&filePath, i, &successAppends,
                                                            &generateThreadMarker,
                                                            &generateAppendMarker]() {
            for (size_t j = 0; j < APPENDS_PER_THREAD; j++) {
                const std::string appendContent = generateThreadMarker(i) + generateAppendMarker(i);
                const auto success = utils::safeFileAppend(filePath, appendContent);
                EXPECT_TRUE(success);
                if (success) {
                    successAppends++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }));
    }

    // Ждем завершения всех потоков добавления
    for (auto &future : appenders) {
        future.wait();
    }

    // Все добавления должны завершиться успешно
    EXPECT_EQ(successAppends, THREAD_COUNT * APPENDS_PER_THREAD);

    // Проверяем, что файл содержит все добавления
    const auto readContent = readFileContent(filePath);
    // Проверяем вхождения маркеров потоков
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        const std::string threadMarker = generateThreadMarker(i);
        EXPECT_NE(readContent.find(threadMarker), std::string::npos);
    }
    // Проверяем вхождения маркеров добавления
    for (size_t i = 0; i < APPENDS_PER_THREAD; i++) {
        const std::string appendMarker = generateAppendMarker(i);
        EXPECT_NE(readContent.find(appendMarker), std::string::npos);
    }
}

// Тест добавления бинарных данных
TEST_F(FileUtilsTest, SafeFileAppendBinary)
{
    const auto filePath = getTestFilePath();
    const std::string initialContent = "Начальный текст";
    createTestFile(filePath, initialContent);

    // Создаем бинарное содержимое с нулевыми байтами
    const std::string binaryContent = createBinaryContent();

    EXPECT_TRUE(utils::safeFileAppend(filePath, binaryContent));

    // Проверяем объединенное содержимое
    const auto readContent = readFileContent(filePath);
    EXPECT_EQ(initialContent + binaryContent, readContent);
}

// Тест базового создания резервной копии
TEST_F(FileUtilsTest, CreateFileBackupBasic)
{
    const auto filePath = getTestFilePath();
    const std::string content = "Content for backup test";
    createTestFile(filePath, content);

    const auto backupPath = utils::createFileBackup(filePath);
    EXPECT_TRUE(backupPath.has_value());
    EXPECT_TRUE(std::filesystem::exists(*backupPath));
    EXPECT_NE(filePath, *backupPath);

    // Проверяем, что содержимое резервной копии соответствует оригиналу
    const auto backupContent = readFileContent(*backupPath);
    EXPECT_EQ(content, backupContent);
}

// Тест резервного копирования несуществующего файла
TEST_F(FileUtilsTest, CreateFileBackupNonExistent)
{
    const auto filePath = getTestFilePath();
    EXPECT_FALSE(std::filesystem::exists(filePath));
    const auto backupPath = utils::createFileBackup(filePath);
    EXPECT_FALSE(backupPath.has_value());
}

// Тест резервного копирования пустого файла
TEST_F(FileUtilsTest, CreateFileBackupEmpty)
{
    const auto filePath = getTestFilePath();
    createTestFile(filePath, "");

    const auto backupPath = utils::createFileBackup(filePath);
    EXPECT_TRUE(backupPath.has_value());
    EXPECT_TRUE(std::filesystem::exists(*backupPath));

    // Проверяем, что резервная копия также пуста
    const auto backupContent = readFileContent(*backupPath);
    EXPECT_TRUE(backupContent.empty());
}

// Тест резервного копирования большого файла
TEST_F(FileUtilsTest, CreateFileBackupLarge)
{
    const auto filePath = getTestFilePath();
    const auto largeContent = generateLargeString(LARGE_FILE_SIZE);
    createTestFile(filePath, largeContent);

    const auto backupPath = utils::createFileBackup(filePath);
    EXPECT_TRUE(backupPath.has_value());
    EXPECT_TRUE(std::filesystem::exists(*backupPath));

    // Проверяем, что содержимое резервной копии соответствует оригиналу
    const auto backupContent = readFileContent(*backupPath);
    EXPECT_EQ(largeContent, backupContent);
}

// Тест резервного копирования бинарного файла
TEST_F(FileUtilsTest, CreateFileBackupBinary)
{
    const auto filePath = getTestFilePath();
    const auto binaryContent = createBinaryContent();
    createTestFile(filePath, binaryContent);

    const auto backupPath = utils::createFileBackup(filePath);
    EXPECT_TRUE(backupPath.has_value());
    EXPECT_TRUE(std::filesystem::exists(*backupPath));

    // Проверяем, что содержимое резервной копии соответствует оригиналу
    const auto backupContent = readFileContent(*backupPath);
    EXPECT_EQ(binaryContent, backupContent);
}

// Тест резервного копирования директории
TEST_F(FileUtilsTest, CreateFileBackupDirectory)
{
    const auto dirPath = testDir / "test_dir";
    createTestDir(dirPath);
    const auto backupPath = utils::createFileBackup(dirPath);
    EXPECT_FALSE(backupPath.has_value());
}

// Тест множественных резервных копий одного и того же файла
TEST_F(FileUtilsTest, CreateFileBackupMultiple)
{
    const auto filePath = getTestFilePath();
    const std::string content = "Content for multiple backup test";
    createTestFile(filePath, content);

    constexpr size_t NUM_BACKUPS = 100;
    std::vector<std::filesystem::path> backupPaths;

    for (size_t i = 0; i < NUM_BACKUPS; i++) {
        const auto backupPath = utils::createFileBackup(filePath);
        EXPECT_TRUE(backupPath.has_value());
        EXPECT_TRUE(std::filesystem::exists(*backupPath));

        // Проверяем, что содержимое резервной копии соответствует оригиналу
        std::string backupContent;
        EXPECT_TRUE(utils::safeFileRead(*backupPath, backupContent));
        EXPECT_EQ(content, backupContent);

        // Проверяем, что эта резервная копия имеет уникальный путь
        for (const auto &existingPath : backupPaths) {
            EXPECT_NE(existingPath, *backupPath);
        }

        backupPaths.push_back(*backupPath);
    }
    EXPECT_EQ(NUM_BACKUPS, backupPaths.size());
}

// Тест параллельного создания резервных копий одного и того же файла
TEST_F(FileUtilsTest, CreateFileBackupConcurrent)
{
    const auto filePath = getTestFilePath();
    const std::string content = "Content for concurrent backup test";
    createTestFile(filePath, content);

    constexpr size_t THREAD_COUNT = 20;
    std::vector<std::future<std::optional<std::filesystem::path>>> backupFutures;

    for (size_t i = 0; i < THREAD_COUNT; i++) {
        backupFutures.push_back(std::async(std::launch::async, [&filePath]() {
            const auto backupPath = utils::createFileBackup(filePath);
            EXPECT_TRUE(backupPath.has_value());
            return backupPath;
        }));
    }

    // Все резервные копии должны успешно создаться и пути к ним должны быть уникальны
    std::unordered_set<std::filesystem::path> backupPaths;
    for (auto &future : backupFutures) {
        auto backupPath = future.get();
        EXPECT_TRUE(backupPath.has_value());
        if (backupPath.has_value()) {
            backupPaths.insert(*backupPath);
        }
    }
    EXPECT_EQ(THREAD_COUNT, backupPaths.size());

    // Все резервные копии должны существовать и соответствовать исходному содержимому
    for (const auto &backupPath : backupPaths) {
        EXPECT_TRUE(std::filesystem::exists(backupPath));
        std::string backupContent;
        EXPECT_TRUE(utils::safeFileRead(backupPath, backupContent));
        EXPECT_EQ(content, backupContent);
    }
}

// Тест резервного копирования несуществующего файла в несуществующей директории
TEST_F(FileUtilsTest, CreateFileBackupNonExistentDirectory)
{
    const auto dirPath = testDir / "nonexistent_dir";
    const auto filePath = dirPath / "test_file.txt";
    EXPECT_FALSE(std::filesystem::exists(dirPath));
    EXPECT_FALSE(std::filesystem::exists(filePath));

    const auto backupPath = utils::createFileBackup(filePath);
    EXPECT_FALSE(backupPath.has_value());
}

// Тест резервного копирования файла без прав на чтение
TEST_F(FileUtilsTest, CreateFileBackupNoReadPermission)
{
#if defined(OCTET_PLATFORM_UNIX)
    const auto filePath = getTestFilePath();
    const std::string content = "Content for backup permission test";
    createTestFile(filePath, content);

    // Убираем права на чтение
    std::filesystem::permissions(filePath, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);

    const auto backupPath = utils::createFileBackup(filePath);
    EXPECT_FALSE(backupPath.has_value());

    // Восстанавливаем права для очистки
    std::filesystem::permissions(filePath, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
#endif
}

// Тест резервного копирования в директории без прав на запись
TEST_F(FileUtilsTest, CreateFileBackupNoWritePermission)
{
#if defined(OCTET_PLATFORM_UNIX)
    const auto restrictedDir = testDir / "restricted_dir";
    createTestDir(restrictedDir);

    const auto filePath = restrictedDir / "test_file.txt";
    const std::string content = "Content for backup permissions test";
    createTestFile(filePath, content);

    // Делаем директорию доступной только для чтения
    std::filesystem::permissions(restrictedDir,
                                 std::filesystem::perms::owner_read
                                     | std::filesystem::perms::group_read
                                     | std::filesystem::perms::others_read,
                                 std::filesystem::perm_options::replace);

    const auto backupPath = utils::createFileBackup(filePath);
    EXPECT_FALSE(backupPath.has_value());

    // Восстанавливаем права для очистки
    std::filesystem::permissions(restrictedDir, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
#endif
}

// Тест создания файла с атомарной записью, добавления к нему, создания резервной копии,
// и затем чтения как оригинала, так и резервной копии
TEST_F(FileUtilsTest, ComplexWriteAppendBackupRead)
{
    const auto filePath = getTestFilePath();
    const std::string initialContent = "Начальное содержимое";
    const std::string appendContent = " - добавленное содержимое";
    EXPECT_FALSE(std::filesystem::exists(filePath));

    // Записываем начальное содержимое
    EXPECT_TRUE(utils::atomicFileWrite(filePath, initialContent));
    EXPECT_TRUE(std::filesystem::exists(filePath));

    // Читаем содержимое
    std::string readContent;
    EXPECT_TRUE(utils::safeFileRead(filePath, readContent));
    EXPECT_EQ(initialContent, readContent);

    // Добавляем дополнительное содержимое
    EXPECT_TRUE(utils::safeFileAppend(filePath, appendContent));

    // Читаем снова для проверки объединенного содержимого
    EXPECT_TRUE(utils::safeFileRead(filePath, readContent));
    EXPECT_EQ(initialContent + appendContent, readContent);

    // 5. Создаем резервную копию
    const auto backupPath = utils::createFileBackup(filePath);
    EXPECT_TRUE(backupPath.has_value());

    // Читаем резервную копию для проверки содержимого
    std::string backupContent;
    EXPECT_TRUE(utils::safeFileRead(*backupPath, backupContent));
    EXPECT_EQ(initialContent + appendContent, backupContent);

    // Изменяем исходный файл
    const std::string newContent = "Modified content";
    EXPECT_TRUE(utils::atomicFileWrite(filePath, newContent));

    // Читаем оба файла для проверки различий в содержимом
    EXPECT_TRUE(utils::safeFileRead(filePath, readContent));
    EXPECT_TRUE(utils::safeFileRead(*backupPath, backupContent));
    EXPECT_EQ(newContent, readContent);
    EXPECT_EQ(initialContent + appendContent, backupContent);
}

// Тест множественных операций в параллельных потоках
TEST_F(FileUtilsTest, ComplexConcurrentOperations)
{
    const auto filesDirPath = testDir / "concurrent_test";

    constexpr size_t THREAD_COUNT = 20;
    constexpr size_t OPERATIONS_PER_THREAD = 10;

    std::mutex filesMutex;
    std::map<std::string, std::string> expectedContents;

    // Запускаем потоки, выполняющие случайные операции
    std::vector<std::future<bool>> futures;
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        futures.push_back(std::async(std::launch::async, [this, i, &filesDirPath, &filesMutex,
                                                          &expectedContents]() {
            // Для определения типа операции
            std::mt19937 gen(std::random_device{}());
            std::uniform_int_distribution<int> opDist(0, 3);

            const std::string threadFilename = "thread_" + std::to_string(i) + ".txt";
            const auto filePath = filesDirPath / threadFilename;

            std::string content;
            bool success = true;

            // Выполняем случайные операции
            for (size_t j = 0; j < OPERATIONS_PER_THREAD; j++) {
                switch (opDist(gen)) {
                // Атомарная запись
                case 0: {
                    const std::string newContent
                        = "[Поток" + std::to_string(i) + "|Запись" + std::to_string(j) + "]";
                    success &= utils::atomicFileWrite(filePath, newContent);

                    if (success) {
                        content = newContent;
                        std::lock_guard<std::mutex> lock(filesMutex);
                        expectedContents[filePath.string()] = content;
                    }
                    break;
                }
                // Безопасное чтение
                case 1: {
                    if (std::filesystem::exists(filePath)) {
                        std::string readContent;
                        success &= utils::safeFileRead(filePath, readContent);

                        if (success) {
                            std::lock_guard<std::mutex> lock(filesMutex);
                            EXPECT_EQ(expectedContents[filePath.string()], readContent);
                        }
                    }
                    break;
                }
                // Добавление
                case 2: {
                    std::string appendContent
                        = "[Поток" + std::to_string(i) + "|Добавление" + std::to_string(j) + "]";
                    success &= utils::safeFileAppend(filePath, appendContent);

                    if (success) {
                        content += appendContent;
                        std::lock_guard<std::mutex> lock(filesMutex);
                        expectedContents[filePath.string()] = content;
                    }
                    break;
                }
                // Создание резервной копии
                case 3: {
                    if (std::filesystem::exists(filePath)) {
                        const auto backupPath = utils::createFileBackup(filePath);
                        if (backupPath.has_value()) {
                            std::string backupContent;
                            success &= utils::safeFileRead(*backupPath, backupContent);

                            if (success) {
                                std::lock_guard<std::mutex> lock(filesMutex);
                                EXPECT_EQ(expectedContents[filePath.string()], backupContent);
                            }
                        }
                        else {
                            success = false;
                        }
                    }
                    break;
                }
                }

                // Добавляем небольшую задержку между операциями
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            return success;
        }));
    }

    // Все потоки должны успешно завершиться
    size_t successCount = 0;
    for (auto &future : futures) {
        if (future.get()) {
            successCount++;
        }
    }
    EXPECT_EQ(successCount, THREAD_COUNT);

    // Проверяем, что все созданные файлы существуют и имеют ожидаемое содержимое
    for (const auto &[path, expectedContent] : expectedContents) {
        const std::filesystem::path filePath(path);
        EXPECT_TRUE(std::filesystem::exists(filePath));
        std::string readContent;
        EXPECT_TRUE(utils::safeFileRead(filePath, readContent));
        EXPECT_EQ(expectedContent, readContent);
    }
}

// Тест сценариев восстановления после ошибок
TEST_F(FileUtilsTest, ComplexErrorRecoveryScenarios)
{
    const auto filePath = getTestFilePath();
    const std::string initialContent
        = "Начальное содержимое для проверки восстановления после сбоев";
    EXPECT_FALSE(std::filesystem::exists(filePath));

    // Создаем начальный файл
    EXPECT_TRUE(utils::atomicFileWrite(filePath, initialContent));
    EXPECT_TRUE(std::filesystem::exists(filePath));

    // Создаем резервную копию
    const auto backupPath = utils::createFileBackup(filePath);
    EXPECT_TRUE(backupPath.has_value());
    EXPECT_TRUE(std::filesystem::exists(*backupPath));

    // Повреждаем исходный файл путем его усечения
    {
        std::ofstream file(filePath, std::ios::trunc);
        file << "Повреждение";
        file.close();
    }

    // Проверяем, что содержимое файлов различается
    std::string originalContent, backupContent;
    EXPECT_TRUE(utils::safeFileRead(filePath, originalContent));
    EXPECT_TRUE(utils::safeFileRead(*backupPath, backupContent));
    EXPECT_NE(originalContent, backupContent);
    EXPECT_EQ(backupContent, initialContent);

    // Восстанавливаем из резервной копии с помощью atomicFileWrite
    EXPECT_TRUE(utils::atomicFileWrite(filePath, backupContent));

    // Проверяем, что восстановление выполнено успешно
    EXPECT_TRUE(utils::safeFileRead(filePath, originalContent));
    EXPECT_EQ(originalContent, initialContent);
}

// Тест с несколькими файлами и сложной структурой директорий
TEST_F(FileUtilsTest, ComplexMultipleFilesAndDirectories)
{
    // Создаем сложную структуру директорий
    const auto baseDir = testDir / "complex_structure";
    const auto dir1 = baseDir / "dir1";
    const auto dir2 = baseDir / "dir2";
    const auto subdir1 = dir1 / "subdir1";
    const auto subdir2 = dir2 / "subdir2";

    EXPECT_FALSE(utils::ensureDirectoryExists(baseDir, false));
    EXPECT_FALSE(utils::ensureDirectoryExists(dir1, false));
    EXPECT_FALSE(utils::ensureDirectoryExists(dir2, false));
    EXPECT_FALSE(utils::ensureDirectoryExists(subdir1, false));
    EXPECT_FALSE(utils::ensureDirectoryExists(subdir2, false));

    EXPECT_TRUE(utils::ensureDirectoryExists(subdir1, true));
    EXPECT_TRUE(utils::ensureDirectoryExists(subdir2, true));

    EXPECT_TRUE(utils::ensureDirectoryExists(baseDir, false));
    EXPECT_TRUE(utils::ensureDirectoryExists(dir1, false));
    EXPECT_TRUE(utils::ensureDirectoryExists(dir2, false));
    EXPECT_TRUE(utils::ensureDirectoryExists(subdir1, false));
    EXPECT_TRUE(utils::ensureDirectoryExists(subdir2, false));

    // Создаем несколько файлов в разных директориях
    std::unordered_map<std::filesystem::path, std::string> testFiles
        = { { baseDir / "root_file.txt", "root file content" },
            { dir1 / "dir1_file.txt", "dir1 file content" },
            { dir2 / "dir2_file.txt", "dir2 file content" },
            { subdir1 / "subdir1_file.txt", "subdir1 file content" },
            { subdir2 / "subdir2_file.txt", "subdir2 file content" } };

    // Записываем начальное содержимое во все файлы
    for (const auto &[path, content] : testFiles) {
        EXPECT_TRUE(utils::atomicFileWrite(path, content));
    }

    // Проверяем, что все файлы созданы с правильным содержимым
    for (const auto &[path, expectedContent] : testFiles) {
        EXPECT_TRUE(std::filesystem::exists(path));

        std::string readContent;
        EXPECT_TRUE(utils::safeFileRead(path, readContent));
        EXPECT_EQ(expectedContent, readContent);
    }

    // Создаем резервные копии всех файлов
    std::unordered_map<std::filesystem::path, std::filesystem::path> backups;
    for (const auto &[path, _] : testFiles) {
        const auto backupPath = utils::createFileBackup(path);
        EXPECT_TRUE(backupPath.has_value());
        EXPECT_TRUE(std::filesystem::exists(*backupPath));
        backups[path] = std::move(*backupPath);
    }

    // Добавляем ко всем файлам
    const std::string appendContent = " - APPENDED";
    std::unordered_map<std::filesystem::path, std::string> updatedTestFiles;
    for (auto &[path, content] : testFiles) {
        EXPECT_TRUE(utils::safeFileAppend(path, appendContent));
        const auto updatedContent = content + appendContent;
        updatedTestFiles[path] = updatedContent;
    }

    // Проверяем содержимое после добавлений
    for (const auto &[path, expectedContent] : updatedTestFiles) {
        std::string readContent;
        EXPECT_TRUE(utils::safeFileRead(path, readContent));
        EXPECT_EQ(expectedContent, readContent);
    }

    // Проверяем, что файлы резервных копий по-прежнему имеют исходное содержимое
    for (const auto &[origPath, backupPath] : backups) {
        std::string backupContent;
        EXPECT_TRUE(utils::safeFileRead(backupPath, backupContent));
        EXPECT_EQ(testFiles[origPath], backupContent);
    }
}

// Тест нагрузки на диск с большим количеством маленьких файлов
TEST_F(FileUtilsTest, ComplexDiskStressWithManyFiles)
{
    constexpr size_t FILES_COUNT = 500;

    // Создаем множество маленьких файлов
    std::unordered_set<std::filesystem::path> filePaths;
    for (size_t i = 0; i < FILES_COUNT; i++) {
        const auto filePath = testDir / ("stress_file_" + std::to_string(i) + ".txt");
        const std::string content
            = "File " + std::to_string(i) + " content: " + std::string(i % 10, 'X');
        filePaths.insert(filePath);
        EXPECT_TRUE(utils::atomicFileWrite(filePath, content));
    }

    // Количество созданных файлов должно соответствовать ожидаемому
    EXPECT_EQ(filePaths.size(), FILES_COUNT);
    // Проверяем, что все файлы существуют
    for (const auto &path : filePaths) {
        EXPECT_TRUE(std::filesystem::exists(path));
    }

    // Создаем резервную копию каждого файла
    std::unordered_set<std::filesystem::path> backupPaths;
    for (const auto &path : filePaths) {
        const auto backupPath = utils::createFileBackup(path);
        EXPECT_TRUE(backupPath.has_value());
        backupPaths.insert(*backupPath);
    }

    // Количество созданных резервных копий должно соответствовать количеству файлов
    EXPECT_EQ(backupPaths.size(), FILES_COUNT);
    // Проверяем, что все резервные копии существуют
    for (const auto &path : backupPaths) {
        EXPECT_TRUE(std::filesystem::exists(path));
    }

    // Случайно изменяем некоторые файлы
    constexpr size_t NUM_MODIFICATIONS = 50;
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, filePaths.size() - 1);
    for (size_t i = 0; i < NUM_MODIFICATIONS; i++) {
        ASSERT_LT(i, filePaths.size());
        auto it = filePaths.begin();
        std::advance(it, i);
        const std::string newContent = "Modified content " + std::to_string(i);
        EXPECT_TRUE(utils::atomicFileWrite(*it, newContent));
    }

    // Проверяем, что все файлы и резервные копии по-прежнему существуют
    for (const auto &path : filePaths) {
        EXPECT_TRUE(std::filesystem::exists(path));
    }

    for (const auto &path : backupPaths) {
        EXPECT_TRUE(std::filesystem::exists(path));
    }
}
} // namespace octet::tests
