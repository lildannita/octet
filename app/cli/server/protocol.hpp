#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>

namespace octet::server {
/**
 * @enum CommandType
 * @brief Типы команд для взаимодействия между Go и C++
 */
enum class CommandType { INSERT, GET, UPDATE, DELETE, UNKNOWN };

/**
 * @struct Request
 * @brief Структура запроса от Go к C++
 */
struct Request {
    std::string requestId;
    CommandType command;
    std::optional<std::string> uuid;
    std::optional<std::string> data;

    /**
     * @brief Десериализация запроса из JSON
     * @param jsonStr JSON-строка
     * @return Request или std::nullopt при ошибке
     */
    static std::optional<Request> fromJson(const std::string &jsonStr);

    /**
     * @brief Конвертация строкового представления команды в CommandType
     * @param cmdStr Строковое представление команды
     * @return Соответствующий CommandType
     */
    static CommandType stringToCommand(const std::string &cmdStr);
};

/**
 * @struct Response
 * @brief Структура ответа от C++ к Go
 */
struct Response {
    std::string requestId;
    bool success;
    std::optional<std::string> uuid;
    std::optional<std::string> data;
    std::optional<std::string> error;

    /**
     * @brief Сериализация ответа в JSON
     * @return JSON-строка
     */
    std::string toJson() const;
};

/**
 * @brief Класс для работы с форматом сообщений по протоколу
 *
 * Формат: [4 байта длины сообщения][JSON-сообщение]
 */
class ProtocolFrame {
public:
    /**
     * @brief Обертывание JSON-сообщения в фрейм протокола
     * @param jsonMessage Сообщение в формате JSON
     * @return Байты фрейма протокола
     */
    static std::vector<uint8_t> wrapMessage(const std::string &jsonMessage);

    /**
     * @brief Извлечение JSON-сообщения из частичного буфера
     * @param buffer Буфер с данными
     * @return std::nullopt, если сообщение неполное, или JSON-сообщение
     */
    static std::optional<std::string> extractMessage(std::vector<uint8_t> &buffer);

    /**
     * @brief Извлечение длины сообщения из заголовка
     * @param headerBytes Байты заголовка (4 байта)
     * @return Длина сообщения
     */
    static uint32_t decodeLength(const uint8_t *headerBytes);

    /**
     * @brief Кодирование длины сообщения в заголовок
     * @param length Длина сообщения
     * @return Байты заголовка (4 байта)
     */
    static std::vector<uint8_t> encodeLength(uint32_t length);
};
} // namespace octet::server
