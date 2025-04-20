#pragma once

#include <atomic>
#include <random>
#include <string>

namespace octet {
/**
 * @class UuidGenerator
 * @brief Генерирует уникальные идентификаторы для строк данных.
 *
 * Генерация идентификатора является гибридной и основана на версии UUID v4,
 * однако не соответствует ей полностью. Использует комбинацию временной
 * метки, случайных чисел и счётчика для создания глобально уникальных
 * идентификаторов.
 */
class UuidGenerator {
public:
    /**
     * @brief Конструктор генератора UUID.
     */
    UuidGenerator();

    /**
     * @brief Генерирует новый уникальный идентификатор.
     * @return Строка с новым UUID.
     */
    std::string generateUuid();

    /**
     * @brief Проверяет корректность формата UUID.
     * @param uuid Проверяемый идентификатор.
     * @return true если формат корректен.
     */
    static bool isValidUuid(const std::string &uuid);

private:
    // Счётчик для обеспечения уникальности
    std::atomic<uint64_t> counter;

    // Генератор случайных чисел
    std::mt19937_64 rng;
};
} // namespace octet
