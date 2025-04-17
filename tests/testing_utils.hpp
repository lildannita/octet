#include <filesystem>
#include <string>

namespace octet::tests {
static constexpr size_t LARGE_FILE_SIZE = 10 * 1024 * 1024; // 10 МБ

/**
 * @brief Генерирует случайное целое число в заданном диапазоне
 * @param min Минимальное значение
 * @param max Максимальное значение
 * @return Случайное число в диапазоне [min, max]
 */
int getRandomInt(int min, int max);

/**
 * @brief Генерирует случайную строку заданной длины
 * @param length Длина строки
 * @return Результат генерации
 */
std::string generateRandomId(size_t length);

/**
 * @brief Генерирует строку указанного размера
 * @param size Размер генерируемой строки
 * @return Сгенерированная строка
 */
std::string generateLargeString(size_t size = LARGE_FILE_SIZE);

/**
 * @brief Создает временную директорию
 * @param suffix Наименование текущего теста
 * @return Путь к временной директории
 */
std::filesystem::path createTmpDirectory(std::string_view suffix = {});

/**
 * @brief Удаляет временную директорию
 * @param tmpDir Путь к временной директории
 */
void removeTmpDirectory(const std::filesystem::path &tmpDir);
} // namespace octet::tests
