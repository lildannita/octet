#include <gtest/gtest.h>
#include <unordered_set>
#include <thread>
#include <future>
#include <vector>
#include <cctype>

#include "storage/uuid_generator.hpp"

namespace octet::tests {

class UuidGeneratorTest : public ::testing::Test {
protected:
    UuidGenerator generator;
};

// Проверка общего формата и структуры UUID
TEST_F(UuidGeneratorTest, UuidTestFormatting)
{
    for (size_t i = 0; i < 100000; i++) {
        const auto uuid = generator.generateUuid();

        EXPECT_TRUE(generator.isValidUuid(uuid));
        EXPECT_EQ(36U, uuid.length());

        // Проверка дефисов
        EXPECT_EQ('-', uuid[8]);
        EXPECT_EQ('-', uuid[13]);
        EXPECT_EQ('-', uuid[18]);
        EXPECT_EQ('-', uuid[23]);

        // Проверка версии UUID
        EXPECT_EQ('4', uuid[14]);

        // Проверка варианта UUID
        const auto version = static_cast<char>(uuid[19]);
        EXPECT_TRUE(version == '8' || version == '9' || version == 'a' || version == 'b');

        // Проверка, что все символы в нижнем регистре
        for (const auto symbol : uuid) {
            EXPECT_TRUE(!std::isalpha(symbol) || std::islower(static_cast<unsigned char>(symbol)));
        }
    }
}

// Проверка уникальности UUID
TEST_F(UuidGeneratorTest, UuidTestUniqueness)
{
    constexpr size_t UUID_COUNT = 100000;
    std::unordered_set<std::string> uuids;

    for (size_t i = 0; i < UUID_COUNT; i++) {
        auto uuid = generator.generateUuid();
        // Проверка правильности UUID
        EXPECT_TRUE(generator.isValidUuid(uuid));
        // Проверка уникальности UUID
        EXPECT_TRUE(uuids.insert(uuid).second);
    }

    EXPECT_EQ(UUID_COUNT, uuids.size());
}

// Проверка многопоточной генерации UUID
TEST_F(UuidGeneratorTest, UuidTestConcurrentGeneration)
{
    constexpr size_t THREAD_COUNT = 20; // Количество потоков
    constexpr size_t UUID_PER_THREAD = 10000; // Количество генерируемых UUID в каждом потоке

    std::unordered_set<std::string> uuids;
    std::mutex uuidsMutex;

    auto generateTask = [this, &uuids, &uuidsMutex]() {
        std::vector<std::string> localUuids;
        for (size_t i = 0; i < UUID_PER_THREAD; i++) {
            auto uuid = generator.generateUuid();
            // Проверка правильности UUID
            EXPECT_TRUE(generator.isValidUuid(uuid));
            localUuids.push_back(uuid);
        }

        std::lock_guard<std::mutex> lock(uuidsMutex);
        for (const auto &uuid : localUuids) {
            // Проверка уникальности UUID
            EXPECT_TRUE(uuids.insert(uuid).second);
        }
    };

    std::vector<std::future<void>> futures;
    for (size_t i = 0; i < THREAD_COUNT; i++) {
        futures.push_back(std::async(std::launch::async, generateTask));
    }

    // Ждем завершения всех потоков
    for (auto &future : futures) {
        future.wait();
    }

    // Дополнительно проверяем, что все UUID уникальны
    EXPECT_EQ(THREAD_COUNT * UUID_PER_THREAD, uuids.size());
}

// Проверка корректных и некорректных UUID
TEST_F(UuidGeneratorTest, UuidTestValidation)
{
    // Валидные UUID
    std::vector<std::string> validUuids
        = { "f47ac10b-58cc-4af8-8f42-51304b7fdc0a", "9e107d9d-3721-4bce-a8c5-f2f89a4a6abc",
            "123e4567-e89b-4d3a-9def-123456789abc", "abcdef12-3456-4bcd-8aaa-abcdefabcdef",
            "0f1e2d3c-4b5a-4c6d-9b8a-000102030405" };
    for (const auto &uuid : validUuids) {
        EXPECT_TRUE(generator.isValidUuid(uuid));
    }

    // Невалидные UUID
    std::vector<std::string> invalidUuids = {
        "123e4567-e89b-12d3-a456", // слишком короткий
        "123e4567-e89b-12d3-a456-4266141740001", // слишком длинный
        "123e4567e89b12d3a456426614174000", // без дефисов
        "123e4567-e89b-12d3-a456-xxxxxxxxxxxx", // не hex
        "123e4567-e89b-1d3a-8456-426614174000", // версия != 4
        "123e4567-e89b-4d3a-c456-426614174000", // вариант некорректен
        "F47AC10B-58CC-4AF8-8F42-51304B7FDC0A", // верхний регистр
        "f47ac10b-58cc-4af8-8F42-51304b7fdc0a", // смешанный регистр
        "f47ac10b-58cc-4af8-8g42-51304b7fdc0a" // недопустимый hex символ
    };
    for (const auto &uuid : invalidUuids) {
        EXPECT_FALSE(generator.isValidUuid(uuid));
    }
}
} // namespace octet::tests
