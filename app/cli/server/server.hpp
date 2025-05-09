#pragma once

#include <filesystem>
#include <memory>
#include <atomic>
#include <boost/asio.hpp>

#include "storage/storage_manager.hpp"

namespace octet::server {
/**
 * @class Server
 * @brief Серверный процесс для обработки запросов от Go
 */
class Server {
public:
    /**
     * @brief Инициализация и запуск сервера
     * @return Код завершения
     */
    static int startServer(StorageManager &storage, std::optional<std::string> socketPath);

private:
    StorageManager &storage_; // Хранилище
    std::filesystem::path socketPath_; // Путь к сокету
    std::unique_ptr<boost::asio::io_context> ioCtx_; // ASIO контекст
    std::unique_ptr<boost::asio::local::stream_protocol::acceptor> acceptor_; // Ассептор соединений
    std::unique_ptr<boost::asio::signal_set> signalSet_; // Обработчик сигналов завершения
    std::atomic<bool> running_; // Флаг работы сервера

    /**
     * @brief Конструктор сервера
     * @param storagePath Путь к хранилищу
     * @param socketPath Путь к Unix Domain Socket
     */
    Server(StorageManager &storage, std::optional<std::string> socketPath);

    /**
     * @brief Деструктор сервера
     */
    ~Server();

    /**
     * @brief Запуск сервера
     * @return Код завершения
     */
    int start();

    /**
     * @brief Остановка сервера
     */
    void stop();

    /**
     * @brief Принятие нового соединения
     */
    void accept();
};
} // namespace octet::server
