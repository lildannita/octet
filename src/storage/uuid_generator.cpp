#include "storage/uuid_generator.hpp"

#include <chrono>
#include <iomanip>
#include <regex>
#include <sstream>

namespace {
// Получение текущего времени в виде количества тиков (единиц времени) с начала эпохи
uint64_t getCurrentTimestampInTicks()
{
    return std::chrono::high_resolution_clock::now().time_since_epoch().count();
}
} // namespace

namespace octet {
UuidGenerator::UuidGenerator()
    : counter(0)
{
    // Инициализация генератора случайных чисел
    rng.seed(getCurrentTimestampInTicks());
}

/**
 * Правила генерации: [xxxxxxxx-yyyy-Mzzz-Nfff-dddddddddddd]
 *  - [xxxxxxxx] (8) — младшие 32 бита временной метки
 *  - [yyyy] (4) — старшие 16 бит временной метки
 *  - [Mzzz] (4):
 *      - [M] = 4 — версия UUID v4
 *      - [zzz] — значение атомарного счетчика
 *  - [Nfff] (4):
 *      - [N] — вариант, значение из набора [8, 9, A, B]
 *      - [fff] — 12 бит случайного числа
 * - [dddddddddddd] (12) — 48 бит случайного числа
 */
std::string UuidGenerator::generateUuid()
{
    // Получение текущего времени с высоким разрешением
    auto timestamp = getCurrentTimestampInTicks();

    // Генерация случайного компонента
    auto random = rng();

    // Увеличение счетчика, не требуем синхронизацию между потоками для увелечения
    auto count = counter.fetch_add(1, std::memory_order_relaxed);

    // Форматирование в формате UUID
    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    // 8 символов из временной метки
    ss << std::setw(8) << (timestamp & 0xFFFFFFFF) << "-";

    // 4 символа из верхних битов временной метки
    ss << std::setw(4) << ((timestamp >> 32) & 0xFFFF) << "-";

    // Версия + 3 символа из счетчика
    ss << "4" << std::setw(3) << (count & 0xFFF) << "-";

    // Вариант + 3 символа из случайного числа
    ss << std::setw(1) << (8 + (random & 0x3)) << std::setw(3) << ((random >> 2) & 0xFFF) << "-";

    // 12 символов из случайного числа
    ss << std::setw(12) << ((random >> 14) & 0xFFFFFFFFFFFF);

    return ss.str();
}

bool UuidGenerator::isValidUuid(const std::string &uuid)
{
    // Регулярное выражение для проверки (регистр всех символов должен быть нижним)
    static const std::regex uuidRegex(
        R"(^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$)");

    return std::regex_match(uuid, uuidRegex);
}
} // namespace octet
