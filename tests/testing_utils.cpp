#include "testing_utils.hpp"

#include <gtest/gtest.h>
#include <mutex>
#include <random>
#include <unordered_set>

#include "utils/logger.hpp"

namespace {
// Мьютекс для безопасного доступа к контейнеру из нескольких потоков
static std::mutex randomIdsMutex;
// Базовая часть наименования временных директорий
static const char tmpDirBase[] = "octet_test_";
} // namespace

namespace octet::tests {
int getRandomInt(int min, int max)
{
    // Для каждого потока создаем свой экземпляр генератора
    thread_local std::mt19937 rng(std::chrono::system_clock::now().time_since_epoch().count());
    // Обеспечиваем равномерное распределение целых чисел в диапазоне [min, max]
    std::uniform_int_distribution<int> dist(min, max);
    // Возвращаем случайное число
    return dist(rng);
}

std::string generateRandomId(size_t length)
{
    static std::unordered_set<std::string> randomIds;

    // Для каждого потока создаем свой экземпляр генератора
    thread_local std::mt19937 rng(std::random_device{}());
    static const char characters[]
        = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    // Обеспечиваем равномерное распределение целых чисел
    // `-2`, так как убираем `\0` из массива + обеспечиваем индексацию от нулевого символа
    std::uniform_int_distribution<size_t> dist(0, sizeof(characters) - 2);

    // Блокируем мьютекс для защиты контейнера со сгенерированными ID
    std::lock_guard<std::mutex> guard(randomIdsMutex);

    bool idUnique = false;
    std::string result;
    result.reserve(length);
    constexpr uint16_t maxAttempts = 10000;
    uint16_t attempts = 0;
    do {
        result.clear();
        // Формируем уникальный ID
        for (size_t i = 0; i < length; i++) {
            result += characters[dist(rng)];
        }
        // Проверяем, был ли сгенерирован такой ID в рамках текущей сессии тестирования
        idUnique = randomIds.find(result) == randomIds.end();
        attempts++;
    } while (!idUnique && attempts < maxAttempts);
    EXPECT_TRUE(idUnique);

    // Сохраняем сгенерированный ID в хранилище
    randomIds.insert(result);
    return result;
}

std::string generateLargeString(size_t size)
{
    std::string result(size, 'X');
    for (size_t i = 0; i < size; i += 1024) {
        size_t pos = i % 26;
        result[i] = static_cast<char>('A' + pos);
    }
    return result;
}

std::filesystem::path createTmpDirectory(std::string_view suffix)
{
    const auto systemTmpDir = std::filesystem::temp_directory_path();
    const auto tmpDirBaseWithSuffix = tmpDirBase + std::string(suffix) + "_";
    std::filesystem::path tmpDir;
    do {
        tmpDir = systemTmpDir / (tmpDirBase + generateRandomId(8));
    } while (std::filesystem::exists(tmpDir));

    // Создаем директорию и проверяем результат
    std::error_code ec;
    bool created = std::filesystem::create_directories(tmpDir, ec);
    EXPECT_TRUE(created);
    EXPECT_FALSE(ec);

    // Проверяем, что директория действительно создана
    EXPECT_TRUE(std::filesystem::exists(tmpDir));
    EXPECT_TRUE(std::filesystem::is_directory(tmpDir));

    return tmpDir;
}

void removeTmpDirectory(const std::filesystem::path &tmpDir)
{
    if (!std::filesystem::exists(tmpDir)) {
        // Директория уже удалена или не была создана
        return;
    }

    // Проверяем, что это действительно временная тестовая директория
    const auto dirName = tmpDir.filename().string();
    EXPECT_TRUE(dirName.find(tmpDirBase) == 0);

    // Удаляем тестовую директорию и все ее содержимое
    std::error_code ec;
    bool removed = std::filesystem::remove_all(tmpDir, ec) > 0;

    // Если возникла ошибка при удалении, логируем ее, но не прерываем тест
    if (ec) {
        LOG_WARNING << "Ошибка при удалении тестовой директории: " << tmpDir.string()
                    << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
    }
    else if (!removed) {
        LOG_WARNING << "Директория не была удалена: " << tmpDir.string();
    }
}
} // namespace octet::tests
