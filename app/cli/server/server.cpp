#include "server.hpp"

#include <iostream>
#include <boost/system/error_code.hpp>

#include "utils/file_utils.hpp"
#include "logger.hpp"

namespace {
std::filesystem::path getSocketPath(std::optional<std::string> socketPath)
{
    if (socketPath.has_value()) {
        return std::filesystem::path(std::move(*socketPath));
    }
    return std::filesystem::temp_directory_path() / "octet.sock";
}
} // namespace

namespace octet::server {
Server::Server(StorageManager &storage, std::optional<std::string> socketPath)
    : socketPath_(getSocketPath(socketPath))
    , storage_(storage)
    , running_(false)
{
}

Server::~Server()
{
    stop();
}

int Server::startServer(StorageManager &storage, std::optional<std::string> socketPath)
{
    Server server(storage, std::move(socketPath));
    return server.start();
}

int Server::start()
{
    // TODO: учитывая, что в итоге start() - блокирующий, нужна ли эта проверка?
    if (running_) {
        LOG_WARNING << "Сервер уже запущен";
        return 0;
    }

    try {
        // Проверяем, что сокет не существует
        std::error_code ec;
        if (std::filesystem::exists(socketPath_, ec)) {
            LOG_ERROR << "Сокет существует: " << socketPath_.string()
                      << ", удалите его вручную и перезапустите программу";
            return false;
        }

        if (!utils::ensureDirectoryExists(socketPath_.parent_path())) {
            LOG_ERROR << "Не удалось обеспечить существование директории для сокета: "
                      << socketPath_.string();
            return false;
        }

        // Инициализируем ASIO контекст
        ioCtx_ = std::make_unique<boost::asio::io_context>();

        // Создаем аксептор
        acceptor_ = std::make_unique<boost::asio::local::stream_protocol::acceptor>(
            *ioCtx_, boost::asio::local::stream_protocol::endpoint(socketPath_.string()));

        // Начинаем принимать соединения
        accept();

        // Настраиваем корректную обработку сигналов
        signalSet_ = std::make_unique<boost::asio::signal_set>(*ioCtx_, SIGINT, SIGTERM);
        signalSet_->async_wait([this](auto ec, auto sig) {
            if (!ec) {
                LOG_IMPORTANT << "Получен сигнал " << sig;
                stop();
            }
        });

        // Устанавливаем флаг работы
        running_ = true;

        LOG_IMPORTANT << "Запуск сервера на сокете " << socketPath_.string() << "...";

        // Запуск рабочего потока (блокирующий вызов)
        ioCtx_->run();

        LOG_IMPORTANT << "Сервер завершил работу";
        return 0;
    }
    catch (const std::exception &e) {
        LOG_ERROR << "Ошибка при запуске сервера: " << e.what();
        return 1;
    }
}

void Server::stop()
{
    if (!running_) {
        return;
    }

    LOG_IMPORTANT << "Останавливаем сервер...";

    // Устанавливаем флаг остановки
    running_ = false;

    if (signalSet_ != nullptr) {
        boost::system::error_code ec;
        signalSet_->cancel(ec);
        if (ec) {
            LOG_ERROR << "Ошибка при отмене регистрации сигналов: " << ec.message();
        }
    }

    // Закрываем аксептор
    if (acceptor_ != nullptr) {
        boost::system::error_code ec;
        acceptor_->close(ec);
        if (ec) {
            LOG_ERROR << "Ошибка при закрытии аксептора: " << ec.message();
        }
        acceptor_.reset();
    }

    // Останавливаем ASIO контекст
    if (ioCtx_ != nullptr) {
        ioCtx_->stop();
    }

    // Удаляем файл сокета
    std::error_code ec;
    std::filesystem::remove(socketPath_, ec);
    if (ec) {
        LOG_ERROR << "Ошибка при удалении существующего сокета: " << socketPath_.string()
                  << ", код ошибки: " << ec.value() << ", сообщение: " << ec.message();
    }

    // Сбрасываем ASIO контекст
    ioCtx_.reset();
}

void Server::accept()
{
    if (acceptor_ == nullptr || ioCtx_ == nullptr) {
        return;
    }

    // Создаем новое соединение
    auto newConnection = Connection::create(*ioCtx_, storage_);

    // Асинхронно принимаем соединение
    acceptor_->async_accept(newConnection->socket(),
                            [this, newConnection](const boost::system::error_code &ec) {
                                if (!ec) {
                                    // Начинаем обрабатывать соединение
                                    newConnection->start();
                                }
                                else if (ec != boost::asio::error::operation_aborted) {
                                    LOG_ERROR << "Ошибка при приеме соединения: " << ec.message();
                                }
                                // Продолжаем принимать соединения, если сервер работает
                                if (running_) {
                                    accept();
                                }
                            });
}
} // namespace octet::server