#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace octet::utils {
/**
 * @brief Потокобезопасное преобразование кода ошибки в строковое описание
 * @return Строковое описание ошибки
 */
extern std::string errnoToString(int errnum);

/**
 * @enum LogLevel
 * @brief Уровни логирования, определяющие важность сообщения
 */
enum class LogLevel {
    TRACE, // Детальная трассировка для отладки
    DEBUG, // Отладочные сообщения
    INFO, // Информационные сообщения
    WARNING, // Предупреждения, не являющиеся ошибками
    ERROR, // Ошибки, не прерывающие работу программы
    CRITICAL // Критические ошибки, прерывающие работу программы
};

/**
 * @class Logger
 * @brief Управляет логированием сообщений с различными уровнями важности
 *
 * Logger является синглтоном и обеспечивает потокобезопасное логирование.
 * По умолчанию логирование отключено и должно быть явно включено пользователем.
 */
class Logger {
public:
    /**
     * @brief Получение единственного экземпляра логгера
     * @return Ссылка на экземпляр логгера
     */
    static Logger &getInstance();

    /**
     * @brief Включает логирование
     * @param logToConsole Включить вывод в консоль
     * @param logFile Путь к файлу для логирования (опционально)
     * @param minLevel Минимальный уровень сообщений для логирования
     * @param useColors Использовать цветной вывод в консоли (если поддерживается)
     */
    void enable(bool logToConsole = true,
                std::optional<std::filesystem::path> logFile = std::nullopt,
                LogLevel minLevel = LogLevel::INFO, bool useColors = true);

    /**
     * @brief Отключает логирование
     */
    void disable();

    /**
     * @brief Проверяет, включено ли логирование
     * @return true, если логирование включено
     */
    bool isEnabled() const;

    /**
     * @brief Установка минимального уровня логирования
     * @param level Минимальный уровень сообщений
     */
    void setMinLogLevel(LogLevel level);

    /**
     * @brief Получение текущего минимального уровня логирования
     * @return Текущий минимальный уровень
     */
    LogLevel getMinLogLevel() const;

    /**
     * @brief Включение или отключение цветного вывода в консоль
     * @param useColors true для использования цветного вывода, false для обычного текста
     */
    void setUseColors(bool useColors);

    /**
     * @brief Проверка, используется ли цветной вывод в консоль
     * @return true, если цветной вывод включен
     */
    bool getUseColors() const;

    /**
     * @brief Логирование сообщения с указанным уровнем
     * @param level Уровень сообщения
     * @param message Текст сообщения
     * @param file Имя файла, из которого вызвана функция логирования
     * @param line Номер строки, из которой вызвана функция логирования
     */
    void log(LogLevel level, const std::string &message, const std::string_view file = {},
             int line = 0);

private:
    // Запрещаем создание экземпляров класса напрямую
    Logger();
    ~Logger();
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

    // Состояние логгера
    bool enabled_; // Включено ли логирование
    bool consoleOutput_; // Вывод в консоль
    bool colorOutput_; // Использовать цветной вывод
    std::optional<std::filesystem::path> logFilePath_; // Путь к файлу лога
    LogLevel minimumLevel_; // Минимальный уровень логирования
    std::mutex logMutex_; // Мьютекс для потокобезопасности

    /**
     * @brief Преобразует уровень логирования в строку
     * @param level Уровень логирования
     * @return Текстовое представление уровня
     */
    static std::string levelToString(LogLevel level);

    /**
     * @brief Форматирует сообщение для вывода в лог
     * @param level Уровень сообщения
     * @param message Текст сообщения
     * @param file Имя файла
     * @param line Номер строки
     * @return Отформатированное сообщение
     */
    std::string formatLogMessage(LogLevel level, const std::string &message,
                                 const std::string_view file, int line) const;

    /**
     * @brief Записывает сообщение в файл лога
     * @param formattedMessage Отформатированное сообщение
     * @return true, если запись выполнена успешно
     */
    bool writeToFile(const std::string &formattedMessage);

    /**
     * @brief Выводит сообщение в консоль
     * @param formattedMessage Отформатированное сообщение
     * @param level Уровень сообщения (для цветового выделения)
     */
    void writeToConsole(const std::string &formattedMessage, LogLevel level);

    /**
     * @brief Проверяет, поддерживает ли консоль ANSI цвета
     * @return true, если консоль поддерживает ANSI цвета
     */
    bool isColorSupportedByTerminal() const;
};

/**
 * @brief Вспомогательный класс для логирования с использованием потокового синтаксиса
 */
class LogStream {
public:
    /**
     * @brief Создает поток логирования для указанного уровня
     * @param level Уровень логирования
     * @param file Имя файла, из которого произведен вызов
     * @param line Номер строки
     */
    LogStream(LogLevel level, const std::string_view file, int line);

    /**
     * @brief Деструктор, который отправляет собранное сообщение в логгер
     */
    ~LogStream();

    /**
     * @brief Оператор перенаправления для потокового формирования сообщения
     * @param val Значение для добавления в сообщение
     * @return Ссылка на текущий поток
     */
    template <typename T> LogStream &operator<<(const T &val)
    {
        if (Logger::getInstance().isEnabled() && level_ >= Logger::getInstance().getMinLogLevel()) {
            stream_ << val;
        }
        return *this;
    }

private:
    LogLevel level_; // Уровень логирования
    std::ostringstream stream_; // Поток для формирования сообщения
    std::string_view file_; // Имя файла
    int line_; // Номер строки
};

} // namespace octet::utils

// Макросы для условного логирования
// TODO: В теории, можно сделать еще эффективнее: настраивать логирование на уровне сборки,
// а тут проверять флаги. Но насколько это будет удобнее - вопрос...
#define LOG_TRACE_ENABLED                                                                          \
    (octet::utils::Logger::getInstance().isEnabled()                                               \
     && octet::utils::Logger::getInstance().getMinLogLevel() <= octet::utils::LogLevel::TRACE)
#define LOG_DEBUG_ENABLED                                                                          \
    (octet::utils::Logger::getInstance().isEnabled()                                               \
     && octet::utils::Logger::getInstance().getMinLogLevel() <= octet::utils::LogLevel::DEBUG)
#define LOG_INFO_ENABLED                                                                           \
    (octet::utils::Logger::getInstance().isEnabled()                                               \
     && octet::utils::Logger::getInstance().getMinLogLevel() <= octet::utils::LogLevel::INFO)
#define LOG_WARNING_ENABLED                                                                        \
    (octet::utils::Logger::getInstance().isEnabled()                                               \
     && octet::utils::Logger::getInstance().getMinLogLevel() <= octet::utils::LogLevel::WARNING)
#define LOG_ERROR_ENABLED                                                                          \
    (octet::utils::Logger::getInstance().isEnabled()                                               \
     && octet::utils::Logger::getInstance().getMinLogLevel() <= octet::utils::LogLevel::ERROR)
#define LOG_CRITICAL_ENABLED                                                                       \
    (octet::utils::Logger::getInstance().isEnabled()                                               \
     && octet::utils::Logger::getInstance().getMinLogLevel() <= octet::utils::LogLevel::CRITICAL)

// Макросы для удобного логирования с автоматическим указанием файла и строки
#define LOG_TRACE                                                                                  \
    if (LOG_TRACE_ENABLED)                                                                         \
    octet::utils::LogStream(octet::utils::LogLevel::TRACE, __FILE__, __LINE__)
#define LOG_DEBUG                                                                                  \
    if (LOG_DEBUG_ENABLED)                                                                         \
    octet::utils::LogStream(octet::utils::LogLevel::DEBUG, __FILE__, __LINE__)
#define LOG_INFO                                                                                   \
    if (LOG_INFO_ENABLED)                                                                          \
    octet::utils::LogStream(octet::utils::LogLevel::INFO, __FILE__, __LINE__)
#define LOG_WARNING                                                                                \
    if (LOG_WARNING_ENABLED)                                                                       \
    octet::utils::LogStream(octet::utils::LogLevel::WARNING, __FILE__, __LINE__)
#define LOG_ERROR                                                                                  \
    if (LOG_ERROR_ENABLED)                                                                         \
    octet::utils::LogStream(octet::utils::LogLevel::ERROR, __FILE__, __LINE__)
#define LOG_CRITICAL                                                                               \
    if (LOG_CRITICAL_ENABLED)                                                                      \
    octet::utils::LogStream(octet::utils::LogLevel::CRITICAL, __FILE__, __LINE__)
