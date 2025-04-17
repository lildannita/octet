#include "storage/storage_manager.hpp"

#include <fstream>
#include <sstream>
#include "utils/file_utils.hpp"
#include "utils/file_lock_guard.hpp"

namespace {
static constexpr char SNAPSHOT_FILE_NAME[] = "octet-data.snapshot";
static constexpr char JOURNAL_FILE_NAME[] = "octet-operations.journal";

// Быстрое преобразование хранилища в строку
std::string serializeMap(const std::unordered_map<std::string, std::string> &map)
{
    // Узнаём число пар в хранилище и приводим к uint32_t
    const auto count = static_cast<uint32_t>(map.size());

    // Считаем общий размер буфера:
    // + 4 байта на count
    // + по 4 байта на длину для каждой пары
    // + длина самих данных
    size_t totalSize = sizeof(count);
    for (auto &[k, v] : map) {
        totalSize += sizeof(uint32_t) + k.size() // длина ключа + ключ
                     + sizeof(uint32_t) + v.size(); // длина значения + значение
    }

    // Резервируем память в итоговой строке, чтобы не было повторных аллокаций
    std::string buf;
    buf.reserve(totalSize);

    // Лямбда для добавления 32‑битного целого в buf
    // TODO: данные записываются в представлении little‑endian, но может для поддержания
    // кроссплатформенности лучше явно использовать big-endian представление
    auto u32Append = [&](uint32_t x) { buf.append(reinterpret_cast<const char *>(&x), sizeof(x)); };

    // Записываем сначала количество элементов
    u32Append(count);
    // Затем уже записываем сами данные хранилища
    for (auto &[k, v] : map) {
        u32Append(static_cast<uint32_t>(k.size())); // длина ключа
        buf.append(k.data(), k.size()); // байты ключа
        u32Append(static_cast<uint32_t>(v.size())); // длина значения
        buf.append(v.data(), v.size()); // байты значения
    }

    return buf;
}

// Быстрое преобразование строки в хранилище
std::optional<std::unordered_map<std::string, std::string>> deserializeMap(const std::string &buf)
{
    // Указатели на начало и конец буфера
    const char *ptr = buf.data();
    const char *end = ptr + buf.size();

    // Проверка недостаточности места для считывания размера
    auto sizeInvalid = [&] { return ptr + sizeof(uint32_t) > end; };
    // Проверка недостаточности места для считывания строки
    auto strSizeInvalid = [&](uint32_t len) { return ptr + len > end; };
    // Чтение размера
    auto readSize = [&] {
        uint32_t size = *reinterpret_cast<const uint32_t *>(ptr);
        ptr += sizeof(uint32_t);
        return size;
    };

    // Удостоверяемся, что достаточно байт для чтения count
    if (sizeInvalid()) {
        return std::nullopt;
    }
    // Читаем count
    const auto count = *reinterpret_cast<const uint32_t *>(ptr);
    ptr += sizeof(uint32_t);

    // Создаём хранилище и сразу резервируем нужное количество бакетов
    std::unordered_map<std::string, std::string> map;
    map.reserve(count);

    // Читаем длину и данные ключа/значения
    for (uint32_t i = 0; i < count; i++) {
        // Считываем ключ
        if (sizeInvalid()) {
            return std::nullopt;
        }
        const auto klen = readSize();
        if (strSizeInvalid(klen)) {
            return std::nullopt;
        }
        std::string key(ptr, klen);
        ptr += klen;

        // Считываем значение
        if (sizeInvalid()) {
            return std::nullopt;
        }
        const auto vlen = readSize();
        if (strSizeInvalid(vlen)) {
            return std::nullopt;
        }
        std::string value(ptr, vlen);
        ptr += vlen;

        // Вставляем пару в хранилище
        map.emplace(std::move(key), std::move(value));
    }

    return map;
}

} // namespace

namespace octet {
StorageManager::StorageManager(const std::filesystem::path &dataDir)
    : dataDir_(dataDir)
    , snapshotPath_(dataDir / SNAPSHOT_FILE_NAME)
    , journalManager_(dataDir / JOURNAL_FILE_NAME)
    , lastSnapshotTime_(std::chrono::steady_clock::now())
{
    LOG_INFO << "Инициализация StorageManager, директория данных: " << dataDir_.string();

    // Создаем директорию для данных, если необходимо
    if (!utils::ensureDirectoryExists(dataDir_)) {
        LOG_CRITICAL << "Не удалось создать директорию данных: " << dataDir_.string();
        throw std::runtime_error("Не удалось создать директорию данных: " + dataDir_.string());
    }

    // Загрузка данных с диска
    if (!loadFromDisk()) {
        LOG_WARNING << "Не удалось полностью загрузить данные с диска";
    }

    // Запуск потока для асинхронных снапшотов
    snapshotThread_ = std::thread(&StorageManager::snapshotThreadFunction, this);
    LOG_INFO << "StorageManager успешно инициализирован, запущен поток снапшотов";
}

StorageManager::~StorageManager()
{
    LOG_INFO << "Завершение работы StorageManager";

    // Сигнал о завершении для потока снапшотов
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        shutdownRequested_ = true;
        snapshotCondition_.notify_all();
    }

    // Ожидание завершения потока
    if (snapshotThread_.joinable()) {
        snapshotThread_.join();
    }

    // Создание финального снапшота перед выходом
    LOG_INFO << "Создание финального снапшота перед завершением работы";
    createSnapshot();

    LOG_INFO << "StorageManager успешно завершил работу";
}

bool StorageManager::loadFromDisk()
{
    LOG_INFO << "Загрузка данных с диска";

    // Проверяем наличие файла снапшота
    bool snapshotLoaded = false;
    if (utils::isFileReadable(snapshotPath_)) {
        LOG_INFO << "Найден файл снапшота, загружаем: " << snapshotPath_.string();
        snapshotLoaded = loadSnapshot();

        if (!snapshotLoaded) {
            LOG_WARNING << "Не удалось загрузить снапшот, продолжаем без него";
        }
    }
    else {
        LOG_INFO << "Файл снапшота не найден, продолжаем без него";
    }

    // Восстанавливаем данные из журнала
    std::optional<std::string> lastCheckpointId = std::nullopt;
    if (snapshotLoaded) {
        // Если снапшот загружен, восстанавливаем операции после последней контрольной точки
        lastCheckpointId = journalManager_.getLastCheckpointId();
    }

    LOG_INFO << "Восстановление из журнала"
             << (lastCheckpointId.has_value() ? (", начиная с точки: " + *lastCheckpointId)
                                              : " всех операций");

    if (!restoreFromJournal(lastCheckpointId)) {
        LOG_WARNING << "Не удалось полностью восстановить данные из журнала";
    }

    LOG_INFO << "Загрузка данных с диска завершена, записей в хранилище: " << dataStore_.size();
    return true;
}

bool StorageManager::loadSnapshot()
{
    LOG_DEBUG << "Загрузка снапшота: " << snapshotPath_.string();

    std::string content;
    if (!utils::safeFileRead(snapshotPath_, content)) {
        LOG_ERROR << "Ошибка чтения файла снапшота";
        return false;
    }

    auto dataStore = deserializeMap(content);
    if (dataStore.has_value()) {
        LOG_INFO << "Снапшот успешно загружен, записей: " << dataStore_.size();
        dataStore_ = std::move(*dataStore);
        return true;
    }

    LOG_ERROR << "Данные снапшота повреждены или имеют некорректный формат";
    return false;
}

bool StorageManager::restoreFromJournal(const std::optional<std::string> &lastCheckpointId)
{
    LOG_DEBUG << "Восстановление данных из журнала операций";
    std::unique_lock<std::shared_mutex> lock(storageMutex_);
    return journalManager_.replayJournal(dataStore_, lastCheckpointId);
}

std::optional<std::string> StorageManager::insert(const std::string &data)
{
    // Эксклюзивная блокировка для записи
    std::unique_lock<std::shared_mutex> lock(storageMutex_);

    // Генерируем UUID
    const auto uuid = uuidGenerator_.generateUuid();
    // Записываем в журнал
    if (!journalManager_.writeInsert(uuid, data)) {
        return std::nullopt;
    }
    // Обновляем данные в памяти
    dataStore_[uuid] = data;

    // Уведомляем о выполнении операции
    notifyOperation();

    LOG_DEBUG << "Успешно добавлена запись с UUID: " << uuid;
    return uuid;
}

std::optional<std::string> StorageManager::get(const std::string &uuid) const
{
    // Разделяемая блокировка для чтения
    std::shared_lock<std::shared_mutex> lock(storageMutex_);
    // Ищем запись в хранилище
    auto it = dataStore_.find(uuid);
    if (it != dataStore_.end()) {
        // Если нашли, возвращаем данные для переданного UUID
        return it->second;
    }
    LOG_DEBUG << "Запись с UUID не найдена: " << uuid;
    return std::nullopt;
}

bool StorageManager::update(const std::string &uuid, const std::string &data)
{
    // Эксклюзивная блокировка для записи
    std::unique_lock<std::shared_mutex> lock(storageMutex_);

    // Проверяем существование записи
    if (dataStore_.find(uuid) == dataStore_.end()) {
        LOG_WARNING << "Попытка обновить несуществующую запись с UUID: " << uuid;
        return false;
    }
    // Записываем в журнал
    if (!journalManager_.writeUpdate(uuid, data)) {
        return false;
    }
    // Обновляем данные в памяти
    dataStore_[uuid] = data;

    // Уведомляем о выполнении операции
    notifyOperation();

    LOG_DEBUG << "Успешно обновлена запись с UUID: " << uuid;
    return true;
}

bool StorageManager::remove(const std::string &uuid)
{
    // Эксклюзивная блокировка для записи
    std::unique_lock<std::shared_mutex> lock(storageMutex_);

    // Проверяем существование записи
    if (dataStore_.find(uuid) == dataStore_.end()) {
        LOG_WARNING << "Попытка удалить несуществующую запись с UUID: " << uuid;
        return false;
    }
    // Записываем в журнал
    if (!journalManager_.writeRemove(uuid)) {
        return false;
    }
    // Удаляем из памяти
    dataStore_.erase(uuid);

    // Уведомляем о выполнении операции
    notifyOperation();

    LOG_DEBUG << "Успешно удалена запись с UUID: " << uuid;
    return true;
}

bool StorageManager::createSnapshot()
{
    LOG_INFO << "Создание снапшота хранилища";

    // Копируем данные
    std::unordered_map<std::string, std::string> dataCopy;
    {
        std::shared_lock<std::shared_mutex> lock(storageMutex_);
        dataCopy = dataStore_;
    }

    // Генерируем идентификатор снапшота
    const auto snapshotId = uuidGenerator_.generateUuid();

    // Записываем снапшот на диск
    if (!writeSnapshotToDisk(dataCopy)) {
        LOG_ERROR << "Ошибка создания снапшота: не удалось записать снапшот на диск";
        return false;
    }

    // Записываем контрольную точку в журнал
    if (!journalManager_.writeCheckpoint(snapshotId)) {
        LOG_ERROR << "Ошибка создания снапшота: не удалось записать операцию в журнал";
        return false;
    }

    // Сбрасываем счетчик операций и обновляем время последнего снапшота
    operationsSinceLastSnapshot_ = 0;
    lastSnapshotTime_ = std::chrono::steady_clock::now();

    LOG_INFO << "Снапшот успешно создан, UUID: " << snapshotId;
    return true;
}

bool StorageManager::writeSnapshotToDisk(const std::unordered_map<std::string, std::string> &data)
{
    LOG_DEBUG << "Запись снапшота на диск: " << snapshotPath_.string();

    // Сериализуем данные
    const auto serializedData = serializeMap(data);

    // Записываем снапшот атомарно
    if (!utils::atomicFileWrite(snapshotPath_, serializedData)) {
        LOG_ERROR << "Не удалось записать снапшот на диск";
        return false;
    }

    LOG_INFO << "Снапшот успешно записан на диск, записей: " << data.size();
    return true;
}

void StorageManager::snapshotThreadFunction()
{
    LOG_INFO << "Запущен поток создания снапшотов";

    while (!shutdownRequested_) {
        bool shouldCreateSnapshot = false;

        {
            std::unique_lock<std::mutex> lock(snapshotMutex_);

            // Ждем уведомления или таймаута
            snapshotCondition_.wait_for(
                lock, std::chrono::minutes(snapshotTimeThresholdMinutes_),
                [this] { return snapshotRequested_ || shutdownRequested_; });

            shouldCreateSnapshot = snapshotRequested_;
            snapshotRequested_ = false;
        }

        // Если запрошен снапшот или прошло достаточное время
        const auto now = std::chrono::steady_clock::now();
        const auto timeSinceLastSnapshot
            = std::chrono::duration_cast<std::chrono::minutes>(now - lastSnapshotTime_);
        const auto timeElapsed
            = timeSinceLastSnapshot.count() >= static_cast<int64_t>(snapshotTimeThresholdMinutes_);
        if (shouldCreateSnapshot || (timeElapsed && operationsSinceLastSnapshot_ > 0)) {
            LOG_INFO << "Создание автоматического снапшота, операций с последнего: "
                     << operationsSinceLastSnapshot_;
            createSnapshot();
        }
    }

    LOG_INFO << "Поток создания снапшотов завершен";
}

void StorageManager::requestSnapshotAsync()
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshotRequested_ = true;
    snapshotCondition_.notify_one();
    LOG_DEBUG << "Запрошено асинхронное создание снапшота";
}

void StorageManager::notifyOperation()
{
    // Увеличиваем счетчик операций
    size_t currentOperations = ++operationsSinceLastSnapshot_;
    // Если достигли порога - запрашиваем снапшот
    if (currentOperations >= snapshotOperationsThreshold_) {
        LOG_DEBUG << "Достигнут порог операций (" << currentOperations << "), запрашиваем снапшот";
        requestSnapshotAsync();
    }
}

size_t StorageManager::getEntriesCount() const
{
    std::shared_lock<std::shared_mutex> lock(storageMutex_);
    return dataStore_.size();
}

void StorageManager::setSnapshotOperationsThreshold(size_t threshold)
{
    snapshotOperationsThreshold_ = threshold;
    LOG_INFO << "Установлен новый порог операций для снапшота: " << threshold;
}

void StorageManager::setSnapshotTimeThreshold(size_t minutes)
{
    snapshotTimeThresholdMinutes_ = minutes;
    LOG_INFO << "Установлен новый временной интервал для снапшота: " << minutes << " минут";
}
} // namespace octet
