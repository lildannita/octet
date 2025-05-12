#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <queue>
#include <boost/asio.hpp>

#include "protocol.hpp"
#include "storage/storage_manager.hpp"

namespace octet::server {
/**
 * @class Connection
 * @brief Класс, обрабатывающий одно соединение с клиентом
 */
class Connection : public std::enable_shared_from_this<Connection> {
public:
    using SharedConnection = std::shared_ptr<Connection>;

    /**
     * @brief Создает новое соединение
     * @param ioCtx ASIO контекст
     * @param storage Ссылка на хранилище
     * @return Указатель на новое соединение
     */
    static SharedConnection create(boost::asio::io_context &ioCtx, StorageManager &storage);

    /**
     * @brief Получить сокет
     * @return Ссылка на сокет
     */
    boost::asio::local::stream_protocol::socket &socket();

    /**
     * @brief Начать обработку соединения
     */
    void start();

private:
    StorageManager &storage_; // Хранилище
    boost::asio::local::stream_protocol::socket socket_; // Сокет
    std::vector<uint8_t> readBuffer_; // Буфер для чтения
    std::queue<std::shared_ptr<std::vector<uint8_t>>> writeQueue_; // Очередь буферов для записи
    std::mutex writeMutex_; // Мьютекс для защиты очереди записи
    bool writeInProgress_; // Выполняется ли в данный момент операция записи

    /**
     * @brief Конструктор
     * @param ioCtx ASIO контекст
     * @param storage Ссылка на хранилище
     */
    Connection(boost::asio::io_context &ioCtx, StorageManager &storage);

    /**
     * @brief Асинхронное чтение данных
     */
    void read();

    /**
     * @brief Обработка сообщений в буфере
     */
    void processMessages();

    /**
     * @brief Постановка данных в очередь на отправку
     * @param response Ответ для отправки
     */
    void write(const Response &response);

    /**
     * @brief Асинхронная запись сообщения из очереди
     */
    void do_write();

    /**
     * @brief Обработка запроса
     * @param request Запрос
     * @return Ответ
     */
    Response handleRequest(const Request &request);
};

} // namespace octet::server
