#include "protocol.hpp"

#include "3rdparty/json.hpp"
#include "logger.hpp"

namespace octet::server {
// Используем nlohmann::json для работы с JSON
using json = nlohmann::json;

std::optional<Request> Request::fromJson(const std::string &jsonStr)
{
    try {
        const auto jsonData = json::parse(jsonStr);

        // Проверка обязательных полей
        if (!jsonData.contains("request_id") || !jsonData.contains("command")
            || !jsonData.contains("params")) {
            LOG_ERROR << "JSON не содержит обязательных полей";
            return std::nullopt;
        }

        Request req;
        req.requestId = jsonData["request_id"].get<std::string>();
        req.command = stringToCommand(jsonData["command"].get<std::string>());

        // Разбор параметров
        const auto &params = jsonData["params"];
        if (params.contains("uuid")) {
            req.uuid = params["uuid"].get<std::string>();
        }

        if (params.contains("data")) {
            req.data = params["data"].get<std::string>();
        }

        return req;
    }
    catch (const json::exception &e) {
        LOG_ERROR << "Ошибка при разборе JSON: " << e.what();
        return std::nullopt;
    }
}

CommandType Request::stringToCommand(const std::string &cmd_str)
{
    if (cmd_str == "insert")
        return CommandType::INSERT;
    if (cmd_str == "get")
        return CommandType::GET;
    if (cmd_str == "update")
        return CommandType::UPDATE;
    if (cmd_str == "remove")
        return CommandType::REMOVE;
    if (cmd_str == "ping")
        return CommandType::PING;
    return CommandType::UNKNOWN;
}

std::string Response::toJson() const
{
    json jsonData;
    jsonData["request_id"] = requestId;
    jsonData["success"] = success;

    json params;
    if (uuid.has_value()) {
        params["uuid"] = *uuid;
    }
    if (data.has_value()) {
        params["data"] = *data;
    }
    jsonData["params"] = params;

    if (error.has_value()) {
        jsonData["error"] = *error;
    }

    return jsonData.dump();
}

std::vector<uint8_t> ProtocolFrame::wrapMessage(const std::string &jsonMessage)
{
    // Вычисляем длину сообщения
    const uint32_t length = static_cast<uint32_t>(jsonMessage.size());

    // Кодируем длину
    auto lengthBytes = encodeLength(length);

    // Создаем результирующий буфер
    const auto lengthBytesSize = lengthBytes.size();
    std::vector<uint8_t> frame(lengthBytesSize + jsonMessage.size());

    // Копируем заголовок и сообщение
    std::copy(lengthBytes.begin(), lengthBytes.end(), frame.begin());
    std::copy(jsonMessage.begin(), jsonMessage.end(), frame.begin() + lengthBytesSize);

    return frame;
}

std::optional<std::string> ProtocolFrame::extractMessage(std::vector<uint8_t> &buffer)
{
    constexpr size_t headerSize = 4; // 4 байта для длины

    // Проверяем, есть ли достаточно данных для чтения заголовка
    if (buffer.size() < headerSize) {
        return std::nullopt;
    }

    // Извлекаем длину сообщения
    const auto messageLength = decodeLength(buffer.data());

    // Проверяем, получили ли мы все сообщение
    if (buffer.size() < headerSize + messageLength) {
        return std::nullopt;
    }

    // Извлекаем сообщение
    std::string message(buffer.begin() + headerSize, buffer.begin() + headerSize + messageLength);

    // Удаляем обработанные данные из буфера
    buffer.erase(buffer.begin(), buffer.begin() + headerSize + messageLength);

    return message;
}

// !! Для кодирования/декодирования используем формат little-endian

uint32_t ProtocolFrame::decodeLength(const uint8_t *headerBytes)
{
    constexpr size_t BYTES_IN_TYPE = sizeof(uint32_t);
    constexpr size_t BITS_PER_BYTE = 8;
    uint32_t length = 0;
    for (size_t i = 0; i < BYTES_IN_TYPE; ++i) {
        length |= static_cast<uint32_t>(headerBytes[i]) << (i * BITS_PER_BYTE);
    }
    return length;
}

std::vector<uint8_t> ProtocolFrame::encodeLength(uint32_t length)
{
    constexpr size_t BYTES_IN_TYPE = sizeof(uint32_t);
    constexpr size_t BITS_PER_BYTE = 8;
    std::vector<uint8_t> bytes(BYTES_IN_TYPE);
    for (size_t i = 0; i < BYTES_IN_TYPE; i++) {
        bytes[i] = (length >> (i * BITS_PER_BYTE)) & 0xFF;
    }
    return bytes;
}
} // namespace octet::server
