#include "logger.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#if defined(OCTET_PLATFORM_WINDOWS)
#include <errno.h>
#elif defined(OCTET_PLATFORM_UNIX)
#include <cerrno>
#endif

#include "utils/compiler.hpp"

namespace {
/**
 * @brief Получение текущего времени в формате для лога
 * @return Строка с текущим временем в формате "YYYY-MM-DD HH:MM:SS.mmm"
 */
std::string getCurrentTimeFormatted()
{
    // Получаем текущее время из системных часов
    auto now = std::chrono::system_clock::now();
    // now -> time_t для использования localtime
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    // Получаем миллисекунды текущей секунды
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S") << '.'
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t_now));
    std::string result(buffer);
    result += "." + std::to_string(ms.count());
    return result;
}

/**
 * @brief Извлекает имя файла из полного пути
 * @param fullPath Полный путь к файлу
 * @return Только имя файла без пути
 */
std::string extractFileName(const std::string_view fullPath)
{
    auto pos = fullPath.find_last_of("/\\");
    if (pos != std::string_view::npos) {
        return std::string(fullPath.substr(pos + 1));
    }
    return std::string(fullPath);
}
} // namespace

/**
 * ANSI коды цветов для консольного вывода
 * Используются для визуального выделения сообщений разного уровня
 */
namespace ConsoleColor {
constexpr const char *RESET = "\033[0m"; // Сброс всех атрибутов
constexpr const char *RED = "\033[31m"; // Красный (для ошибок)
constexpr const char *GREEN = "\033[32m"; // Зеленый (для информации)
constexpr const char *YELLOW = "\033[33m"; // Желтый (для предупреждений)
constexpr const char *BLUE = "\033[34m"; // Синий (для отладки)
constexpr const char *MAGENTA = "\033[35m"; // Пурпурный (для критических ошибок)
constexpr const char *CYAN = "\033[36m"; // Голубой (для трассировки)
} // namespace ConsoleColor

namespace octet {
std::string errnoToString(int errnum)
{
    char buffer[128] = { 0 };
#if defined(OCTET_PLATFORM_WINDOWS)
    if (strerror_s(buffer, sizeof(buffer), errnum) == 0) {
        return std::string(buffer);
    }
    else {
        return "Unknown error";
    }
#elif defined(OCTET_PLATFORM_UNIX)
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
    // GNU-версия strerror_r возвращает char*
    return std::string(strerror_r(errnum, buffer, sizeof(buffer)));
#else
    // XSI-совместимая версия strerror_r возвращает int
    if (strerror_r(errnum, buffer, sizeof(buffer)) == 0) {
        return std::string(buffer);
    }
    else {
        return "Unknown error";
    }
#endif
#else
    UNREACHABLE("Unsupported platform");
#endif
}

Logger &Logger::getInstance()
{
    // Реализация синглтона Logger
    static Logger instance;
    return instance;
}

Logger::Logger()
    : enabled_(false)
    , consoleOutput_(false)
    , colorOutput_(false)
    , logFilePath_(std::nullopt)
    , minimumLevel_(LogLevel::INFO)
{
}

Logger::~Logger()
{
}

void Logger::enable(bool logToConsole, std::optional<std::filesystem::path> logFile,
                    LogLevel minLevel, bool useColors)
{
    std::ostringstream configMsg;
    bool needColorWarning = false;

    {
        std::lock_guard<std::mutex> lock(logMutex_);

        enabled_ = true;
        consoleOutput_ = logToConsole;
        logFilePath_ = logFile;
        minimumLevel_ = minLevel;

        // Устанавливаем использование цветов, если это запрошено и поддерживается
        if (useColors) {
            const auto isColorSupported = isColorSupportedByTerminal();
            needColorWarning = !isColorSupported;
            colorOutput_ = useColors && isColorSupported;
        }

        if (logFilePath_.has_value()) {
            // Создаем директорию для лог-файла, если она не существует
            auto dir = logFilePath_->parent_path();
            if (!dir.empty()) {
                std::filesystem::create_directories(dir);
            }

            // Записываем заголовок при инициализации лог-файла
            std::ofstream file(*logFilePath_, std::ios::out | std::ios::app);
            if (file) {
                file << "--- OCTET логирование начато в " << getCurrentTimeFormatted() << " ---"
                     << std::endl;
                file.close();
            }
        }

        // Формируем сообщение с конфигурацией логгера
        configMsg << "Логирование включено (минимальный уровень: " << levelToString(minimumLevel_)
                  << ", вывод в консоль: " << (consoleOutput_ ? "да" : "нет")
                  << ", цветной вывод: " << (colorOutput_ ? "да" : "нет") << ")";
    }

    if (needColorWarning) {
        log(LogLevel::WARNING,
            "Включена поддержка цветного вывода, однако текущая консоль не поддерживает ANSI цвета",
            __FILE__, __LINE__);
    }
    log(LogLevel::INFO, configMsg.str(), __FILE__, __LINE__);
}

void Logger::disable()
{
    std::lock_guard<std::mutex> lock(logMutex_);
    log(LogLevel::INFO, "Логирование отключено", __FILE__, __LINE__);
    enabled_ = false;
}

bool Logger::isEnabled() const
{
    return enabled_;
}

void Logger::setMinLogLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(logMutex_);
    minimumLevel_ = level;

    if (enabled_) {
        std::ostringstream oss;
        oss << "Минимальный уровень логирования установлен на " << levelToString(level);
        log(LogLevel::INFO, oss.str(), __FILE__, __LINE__);
    }
}

LogLevel Logger::getMinLogLevel() const
{
    return minimumLevel_;
}

void Logger::setUseColors(bool useColors)
{
    std::lock_guard<std::mutex> lock(logMutex_);

    // Устанавливаем использование цветов, если это запрошено и поддерживается
    const auto isColorSupported = isColorSupportedByTerminal();
    if (useColors && !isColorSupported) {
        log(LogLevel::WARNING,
            "Включена поддержка цветного вывода, однако текущая консоль не поддерживает ANSI "
            "цвета",
            __FILE__, __LINE__);
    }
    colorOutput_ = useColors && isColorSupported;

    log(LogLevel::INFO,
        "Использование цветного вывода " + std::string(colorOutput_ ? "включено" : "отключено"),
        __FILE__, __LINE__);
}

bool Logger::getUseColors() const
{
    return colorOutput_;
}

void Logger::log(LogLevel level, const std::string &message, const std::string_view file, int line)
{
    // Проверяем, включено ли логирование и подходит ли уровень сообщения
    if (!enabled_ || level < minimumLevel_) {
        return;
    }

    std::lock_guard<std::mutex> lock(logMutex_);

    // Форматируем сообщение
    auto formattedMessage = formatLogMessage(level, message, file, line);

    // Выводим в консоль, если необходимо
    if (consoleOutput_) {
        writeToConsole(formattedMessage, level);
    }

    // Записываем в файл, если указан путь
    if (logFilePath_.has_value()) {
        writeToFile(formattedMessage);
    }
}

std::string Logger::levelToString(LogLevel level)
{
    switch (level) {
    case LogLevel::TRACE:
        return "TRACE";
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARNING:
        return "WARNING";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::CRITICAL:
        return "CRITICAL";
    default:
        UNREACHABLE("Unsopported LogLevel");
    }
}

std::string Logger::formatLogMessage(LogLevel level, const std::string &message,
                                     const std::string_view file, int line) const
{
    std::ostringstream oss;

    // Формат для файла: [ВРЕМЯ] [УРОВЕНЬ] [ФАЙЛ:СТРОКА] Сообщение
    // Для консоли добавляем префикс OCTET
    oss << "[" << getCurrentTimeFormatted() << "] "
        << "[" << levelToString(level) << "] ";

    if (!file.empty()) {
        oss << "[" << extractFileName(file) << ":" << line << "] ";
    }

    oss << message;

    return oss.str();
}

bool Logger::writeToFile(const std::string &formattedMessage)
{
    if (!logFilePath_.has_value()) {
        return false;
    }

    // Открываем файл в режиме добавления
    std::ofstream file(*logFilePath_, std::ios::out | std::ios::app);
    if (!file) {
        std::cerr << "OCTET: Не удалось открыть файл для записи: " << *logFilePath_ << std::endl;
        return false;
    }

    // Записываем сообщение и добавляем перевод строки
    file << formattedMessage << std::endl;
    return true;
}

void Logger::writeToConsole(const std::string &formattedMessage, LogLevel level)
{
    // Префикс для консольного вывода
    const std::string prefix = "OCTET: ";

    // Используем цветовой вывод в зависимости от уровня логирования и настроек
    if (colorOutput_) {
        const char *colorCode = ConsoleColor::RESET;

        switch (level) {
        case LogLevel::TRACE:
            colorCode = ConsoleColor::CYAN;
            break;
        case LogLevel::DEBUG:
            colorCode = ConsoleColor::BLUE;
            break;
        case LogLevel::INFO:
            colorCode = ConsoleColor::GREEN;
            break;
        case LogLevel::WARNING:
            colorCode = ConsoleColor::YELLOW;
            break;
        case LogLevel::ERROR:
            colorCode = ConsoleColor::RED;
            break;
        case LogLevel::CRITICAL:
            colorCode = ConsoleColor::MAGENTA;
            break;
        default:
            UNREACHABLE("Unsupported LogLevel");
        }

        // Выводим сообщение с цветом
        std::cerr << colorCode << prefix << formattedMessage << ConsoleColor::RESET << std::endl;
    }
    else {
        // Выводим сообщение без цвета
        std::cerr << prefix << formattedMessage << std::endl;
    }
}

bool Logger::isColorSupportedByTerminal() const
{
#if defined(OCTET_PLATFORM_UNIX)
    // В Unix-подобных системах проверяем переменную окружения TERM
    const char *term = std::getenv("TERM");
    if (term == nullptr) {
        return false;
    }

    // Проверяем стандартные терминалы, поддерживающие цвет
    return std::string(term) != "dumb" && std::string(term) != "unknown";
#elif defined(OCTET_PLATFORM_WINDOWS)
    // TODO: проверить, нашел нормальную информацию только про Windows10

    // В Windows проверяем наличие поддержки ANSI через GetConsoleMode
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) {
        return false;
    }

    // Проверяем, установлен ли флаг ENABLE_VIRTUAL_TERMINAL_PROCESSING
    // В Windows 10 этот флаг указывает на поддержку ANSI цветов
    return (dwMode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    UNREACHABLE("Unsupported platform");
#endif
}

LogStream::LogStream(LogLevel level, const std::string_view file, int line)
    : level_(level)
    , file_(file)
    , line_(line)
{
}

LogStream::~LogStream()
{
    // Отправляем собранное сообщение в логгер при уничтожении объекта
    // Это позволяет использовать потоковый синтаксис для логирования
    Logger::getInstance().log(level_, stream_.str(), file_, line_);
}

} // namespace octet
