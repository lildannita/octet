#include "connection.hpp"

#include "logger.hpp"

namespace octet::server {
// Максимальный размер буфера чтения (16 КБ)
constexpr uint16_t MAX_BUFFER_SIZE = 16384;

Connection::SharedConnection Connection::create(boost::asio::io_context &io_context,
                                                StorageManager &storage)
{
    return SharedConnection(new Connection(io_context, storage));
}

Connection::Connection(boost::asio::io_context &io_context, StorageManager &storage)
    : socket_(io_context)
    , storage_(storage)
{
    // Резервируем место для чтения
    readBuffer_.reserve(MAX_BUFFER_SIZE);
}

boost::asio::local::stream_protocol::socket &Connection::socket()
{
    return socket_;
}

void Connection::start()
{
    LOG_DEBUG << "Новое соединение установлено";
    read();
}

void Connection::read()
{
    // Предотвращаем уничтожение указателя во время чтения
    auto self(shared_from_this());

    // Создаем временный буфер
    auto tmpBuffer = std::make_shared<std::vector<uint8_t>>(1024);

    // Асинхронное чтение
    socket_.async_read_some(
        boost::asio::buffer(*tmpBuffer), [this, self = shared_from_this(), tmpBuffer](
                                             boost::system::error_code ec, std::size_t length) {
            // Обрабатываем ошибки
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    LOG_ERROR << "Ошибка при чтении: " << ec.message();
                }
                return; // Не продолжаем цикл чтения при ошибке
            }

            // Проверяем, что получили данные
            if (length == 0) {
                LOG_WARNING << "Получены данные нулевой длины, продолжаем чтение";
                read(); // Повторяем чтение
                return;
            }

            // Чистим буффер, если у нас переполнение
            if (readBuffer_.size() + length > MAX_BUFFER_SIZE) {
                LOG_ERROR << "Переполнение буфера чтения, очищаем";
                readBuffer_.clear();
            }
            // Добавляем данные в буфер
            readBuffer_.insert(readBuffer_.end(), tmpBuffer->begin(), tmpBuffer->begin() + length);

            // Обрабатываем сообщения из буфера
            processMessages();

            // Продолжаем чтение
            read();
        });
}

void Connection::processMessages()
{
    while (true) {
        const auto jsonMessage = ProtocolFrame::extractMessage(readBuffer_);
        if (!jsonMessage.has_value()) {
            break; // Нет полных сообщений, выходим из цикла
        }
        LOG_DEBUG << "Извлечено сообщение: " << *jsonMessage;

        // Разбираем запрос
        const auto request = Request::fromJson(*jsonMessage);
        if (request.has_value()) {
            // Обрабатываем запрос и отправляем ответ
            const auto response = handleRequest(*request);
            write(response);
        }
        else {
            LOG_ERROR << "Некорректный формат запроса: " << *jsonMessage;
            // Отправляем ошибку
            Response errorResponse;
            errorResponse.requestId = "error";
            errorResponse.success = false;
            errorResponse.error = "Invalid request format";
            write(errorResponse);
        }
    }
}

void Connection::write(const Response &response)
{
    // Сериализуем ответ в JSON
    const auto jsonResponse = response.toJson();

    // Создаем новый буфер
    auto buffer = std::make_shared<std::vector<uint8_t>>(ProtocolFrame::wrapMessage(jsonResponse));
    // Помещаем буфер в очередь
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writeQueue_.push(buffer);
    }

    // Если запись уже идет, то выходим - текущая операция запустит следующую при завершении
    if (writeInProgress_) {
        return;
    }
    // Запускаем процесс отправки
    do_write();
}

void Connection::do_write()
{
    // Предотвращаем уничтожение указателя во время записи
    auto self(shared_from_this());

    // Блокируем доступ к очереди на время проверки и извлечения
    std::shared_ptr<std::vector<uint8_t>> buffer;
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        // Если очередь пуста, отмечаем, что запись не выполняется, и выходим
        if (writeQueue_.empty()) {
            writeInProgress_ = false;
            return;
        }
        // Отмечаем, что запись выполняется
        writeInProgress_ = true;
        // Получаем буфер из начала очереди
        buffer = writeQueue_.front();
        writeQueue_.pop();
    }

    // Асинхронная запись данных
    boost::asio::async_write(socket_, boost::asio::buffer(*buffer),
                             [this, self, buffer](boost::system::error_code ec, std::size_t) {
                                 if (ec && ec != boost::asio::error::operation_aborted) {
                                     LOG_ERROR << "Ошибка при записи: " << ec.message();
                                 }
                                 // Запускаем следующую операцию записи
                                 do_write();
                             });
}

Response Connection::handleRequest(const Request &request)
{
    Response response;
    response.requestId = request.requestId;
    response.success = true;

    try {
        switch (request.command) {
        case CommandType::INSERT: {
            if (!request.data.has_value()) {
                response.success = false;
                response.error = "Missing data for INSERT";
                break;
            }

            auto result = storage_.insert(*request.data);
            if (result.has_value()) {
                response.uuid = std::move(*result);
            }
            else {
                response.success = false;
                response.error = "Failed to insert data";
            }
            break;
        }
        case CommandType::GET: {
            if (!request.uuid.has_value()) {
                response.success = false;
                response.error = "Missing uuid for GET";
                break;
            }

            auto result = storage_.get(*request.uuid);
            if (result.has_value()) {
                response.data = std::move(*result);
            }
            else {
                response.success = false;
                response.error = "Data not found";
            }
            break;
        }
        case CommandType::UPDATE: {
            if (!request.uuid.has_value() || !request.data.has_value()) {
                response.success = false;
                response.error = "Missing UUID or data for UPDATE";
                break;
            }

            const auto result = storage_.update(*request.uuid, *request.data);
            if (!result) {
                response.success = false;
                response.error = "Failed to update item";
            }
            break;
        }
        case CommandType::REMOVE: {
            if (!request.uuid.has_value()) {
                response.success = false;
                response.error = "Missing uuid for REMOVE";
                break;
            }

            const auto result = storage_.remove(*request.uuid);
            if (!result) {
                response.success = false;
                response.error = "Failed to remove item";
            }
            break;
        }
        case CommandType::PING: {
            break;
        }
        case CommandType::UNKNOWN:
        default: {
            response.success = false;
            response.error = "Unknown command";
            break;
        }
        }
    }
    catch (const std::exception &e) {
        response.success = false;
        response.error = std::string("Exception: ") + e.what();
        LOG_ERROR << "Исключение при обработке запроса: " << e.what();
    }

    return response;
}
} // namespace octet::server
