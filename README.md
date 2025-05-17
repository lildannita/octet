# 📦 octet — безопасное хранилище UTF-8 строк

![Version](https://img.shields.io/badge/version-0.2.0-yellow.svg?style=for-the-badge&logo=semver&logoColor=white) ![Linux](https://img.shields.io/badge/Linux-supported-brightgreen.svg?style=for-the-badge&logo=linux&logoColor=white) ![macOS](https://img.shields.io/badge/macOS-supported-black.svg?style=for-the-badge&logo=apple&logoColor=white) ![Docker](https://img.shields.io/badge/Docker-supported-2496ED.svg?style=for-the-badge&logo=docker&logoColor=white)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg?style=for-the-badge&logo=cplusplus&logoColor=white) ![CMake](https://img.shields.io/badge/CMake-3.17%2B-064F8C.svg?style=for-the-badge&logo=cmake&logoColor=white) ![Go](https://img.shields.io/badge/Go-1.24-00ADD8.svg?style=for-the-badge&logo=go&logoColor=white)
![License](https://img.shields.io/badge/license-GPLv3-red.svg?style=for-the-badge&logo=gnu&logoColor=white)

---

## 🚀 Что это такое?

> **octet** — это прежде всего **библиотека** на C++17, предназначенная для надёжного и высокопроизводительного хранения UTF-8 строк с использованием UUID, механизма WAL и снапшотов.

> В дополнение к библиотеке проект включает готовые инструменты: **удобное CLI-приложение** и **HTTP-сервер на Go**, которые позволяют сразу же начать работу с хранилищем без дополнительной разработки.

---
## 📖 Оглавление

- [📦 octet — безопасное хранилище UTF-8 строк](#-octet--безопасное-хранилище-utf-8-строк)
  - [🚀 Что это такое?](#-что-это-такое)
  - [📖 Оглавление](#-оглавление)
  - [📚 Требования для установки](#-требования-для-установки)
  - [🧠 Основные особенности](#-основные-особенности)
  - [🏗️ Как это работает](#️как-это-работает)
  - [🗂️ Использование octet в проектах](#️-использование-octet-в-проектах)
  - [📌 Обзор модулей](#-обзор-модулей)
    - [⚙️ Пример: `StorageManager`](#️пример-storagemanager)
  - [🖥️ CLI‑приложение](#️cliприложение)
    - [🚀 Пример выполнения одноразовой команды](#-пример-выполнения-одноразовой-команды)
    - [💬 Пример работы в интерактивном режиме](#-пример-работы-в-интерактивном-режиме)
  - [🌐 HTTP-сервер](#http-сервер)
    - [📤 Основные запросы](#-основные-запросы)
    - [🩺 Health‑check](#-healthcheck)
    - [📘 OpenAPI](#-openapi)
  - [🐳 Docker-контейнер](#-docker-контейнер)
  - [🛠️ Makefile — сборка и установка](#️makefile--сборка-иустановка)
    - [🎯 Основные цели:](#-основные-цели)
    - [🧩 Переменные конфигурации:](#-переменные-конфигурации)
  - [📄 Лицензия](#-лицензия)

---

## 📚 Требования для установки

Для успешной сборки и установки библиотеки необходимы:

- **CMake** (>= 3.17)
    
- **Make**
    
- **Ninja** (рекомендуется)
    
- **Компилятор C++17** (gcc / clang)
    
- **Threads** (обычно входит в стандартный состав средств для C++ разработки)

Для сборки **CLI-приложения** также требуется:

- **Boost** (с модулем `system`)

Для сборки **Go-сервера**:

- **Go 1.24+**
    
- Зависимости, получаемые через `go mod tidy`

---

## 🧠 Основные особенности

- 💾 Библиотека поддерживает **четыре базовых операции**:
	- ✅ **INSERT** — добавление новой строки с генерацией UUID.
	- ✅ **GET** — получение строки по UUID.
	- ✅ **UPDATE** — обновление данных по UUID.
	- ✅ **REMOVE** — удаление строки по UUID.

- ⚡ **Высокая производительность** за счёт хранения данных в памяти (`unordered_map`).
    
- 🛡️ **Надёжность и отказоустойчивость** за счёт Write-Ahead Logging и контрольных точек (снимков).
    
- 🔒 **Атомарные операции** записи, защищающие данные при многопоточном доступе.
    
- 📌 **Кроссплатформенность**: Linux, macOS *(поддержка Windows будет добавлена в следующих версиях)*.
    
- 🖥️ **CLI-приложение & HTTP-сервер** — выбирайте удобный интерфейс для быстрой работы с хранилищем.

---

## 🏗️ Как это работает

1. ✍️ **Write‑Ahead Log (WAL)** — операция над хранилищем сперва попадает в журнал, потом в память.
    
2. 🖼️ **Снапшоты** — «снимок» состояния создаётся автоматически:
    - по количеству операций (`--snapshot-operations`),
    - по таймеру (`--snapshot-minutes`),
    - вручную командой `snapshot`,
    - перед остановкой.
    
3. 🩹 **Восстановление** — при перезапуске читается последний снапшот + выполняются действия из журнала, начиная с последнего `CHECKPOINT`, то есть, начиная с отметки выполнения последнего снапшота. 

---

## 🗂️ Использование octet в проектах

После установки библиотеки octet в систему, ее можно подключить через, например, CMake:
```cmake
find_package(octet REQUIRED)
target_link_libraries(your_target PRIVATE octet::shared)
# Или, если нужна статическая линковка
# (при условии, что статическая версия octet собрана и установлена):
# target_link_libraries(your_target PRIVATE octet::static)
```

После чего можно подключать заголовочные файлы библиотеки в свой проект:
```cpp
#include <octet> // Подключает все компоненты

// Или отдельные модули
#include <octet/storage_manager>
#include <octet/journal_manager>
#include <octet/uuid_generator>
#include <octet/logger>
```

---

## 📌 Обзор модулей

| Модуль                                                                                                           | Ключевой класс   | Назначение                                                                                     |
| ---------------------------------------------------------------------------------------------------------------- | ---------------- | ---------------------------------------------------------------------------------------------- |
| [`<octet/storage_manager>`](https://github.com/lildannita/octet/blob/master/include/storage/storage_manager.hpp) | `StorageManager` | Высокоуровневый API: CRUD‑операции, настройка порогов снапшотов, загрузка/сохранение.          |
| [`<octet/journal_manager>`](https://github.com/lildannita/octet/blob/master/include/storage/journal_manager.hpp) | `JournalManager` | Формирование WAL.                                                                              |
| [`<octet/uuid_generator>`](https://github.com/lildannita/octet/blob/master/include/storage/uuid_generator.hpp)   | `UuidGenerator`  | Быстрая генерация UUID v4.                                                                     |
| [`<octet/logger>`](https://github.com/lildannita/octet/blob/master/include/logger.hpp)                           | `Logger`         | Потокобезопасный логгер с уровнями: `TRACE`, `DEBUG`, `INFO`, `WARNING`, `ERROR`, `CRITICAL`.. |
### ⚙️ Пример: `StorageManager`

```cpp
#include <octet>

int main() {
    octet::StorageManager storage("data/");

    auto uuid = storage.insert("{\"name\": \"Danil Goldyshev\"}");
    if (uuid.has_value()) {
        auto data = storage.get(*uuid);
        if (data.has_value()) {
            std::cout << "Data: " << *data << std::endl;
        }
        else {
	        // Идентификатор не найден
        }

        storage.update(*uuid, "{\"name\": \"lildannita\"}");
        storage.remove(*uuid);
    }
    else {
	    // Ошибка при вставке данных
    }

    return 0;
}
```

> Все модули библиотеки на C++ снабжены подробными комментариями в заголовочных файлах. Рекомендуем обращаться напрямую к исходному коду — это лучший способ понять внутреннюю архитектуру и способы использования каждого компонента.

---

## 🖥️ CLI‑приложение

Название исполняемого файла — **`octet`**.
При локальной сборке файл лежит по пути **`/path/to/octet/repo/build/bin/octet`**.
При установке в систему файл лежит по пути **`/usr/local/bin/octet`** (по умолчанию).

### 🚀 Пример выполнения одноразовой команды

```bash
octet --storage=~/octet-storage insert "Hello, cli‑world!"
```

### 💬 Пример работы в интерактивном режиме

```bash
octet --storage=~/octet-storage --interactive
Octet - интерактивный режим  
Введите команду или 'help' для получения справки, 'exit' для выхода
octet> insert {data:"Hi"}
ff538ee9-1f5b-4000-9125-299eb135cfdb
octet> get ff538ee9-1f5b-4000-9125-299eb135cfdb
{data:"Hi"}
octet> snapshot
octet> exit
Выход из интерактивного режима
```

**Для отображения справки по всем командам используйте:** `octet --help`.

---

## 🌐 HTTP-сервер

HTTP-сервер, реализованный на Go, предоставляет удобный API для работы с хранилищем.

Название исполняемого файла — **`octet-server`**.
При локальной сборке файл лежит по пути **`/path/to/octet/repo/build/bin/octet-server`**.
При установке в систему файл лежит по пути **`~/go/bin/octet`** (по умолчанию).

Пример запуска HTTP-сервера:

```bash
octet-server --config=/path/to/config.json --log-level=warn
```

Пример конфигурации запуска сервера по-умолчанию находится по пути [`app/server/config.json`](https://github.com/lildannita/octet/blob/master/app/server/config.json). 
### 📤 Основные запросы

Запросы начинаются с `http://<host>:<port>/octet/v1/…`

| Метод    | URL       | Тело (JSON)         | Описание                          |
| -------- | --------- | ------------------- | --------------------------------- |
| `POST`   | `/`       | `{ "data": "..." }` | Добавить строку (`octet::insert`) |
| `GET`    | `/{uuid}` | —                   | Получить строку (`octet::get`)    |
| `PUT`    | `/{uuid}` | `{ "data": "..." }` | Обновить строку (`octet::update`) |
| `DELETE` | `/{uuid}` | —                   | Удалить строку (`octet::remove`)  |

### 🩺 Health‑check

```bash
curl http://<host>:<port>/health
# {"status":"ok","timestamp":"2025-05-16T22:43:17Z"}
```

### 📘 OpenAPI

HTTP-сервер предоставляет документацию по API в формате OpenAPI (Swagger). После запуска сервера документация будет доступна по адресу:
`http://<host>:<port>/swagger/index.html`

---

## 🐳 Docker-контейнер

Проект содержит [Dockerfile](https://github.com/lildannita/octet/blob/master/docker/Dockerfile), предназначенный для сборки нужного образа, и [docker-compose.yaml](https://github.com/lildannita/octet/blob/master/docker/docker-compose.yaml), в котором представлена достаточная конфигурация запуска Docker-контейнера. По мере необходимости в этой конфигурации можно изменить точку монтирования директории с хранилищем и проброс порта:
```yaml
    ports:
      # Проброс порта API сервера на хост-машину
      - "8080:8080"
    volumes:
      # Монтирование директории для персистентного хранения данных
      - ./octet-storage:/storage
```

---

## 🛠️ Makefile — сборка и установка

### 🎯 Основные цели:

| Цель             | Что делает                                                                           |
| ---------------- | ------------------------------------------------------------------------------------ |
| `all`            | Сборка C++ библиотеки, CLI и Go‑сервера                                              |
| `build`          | Сборка **только** C++ библиотеки                                                     |
| `build-cli`      | Сборка CLI‑бинарника                                                                 |
| `install`        | Установка C++ библиотеки (требует `sudo`)                                            |
| `install-app`    | Установка _всего набора_: библиотека + CLI + Go (требует `sudo`)                     |
| `uninstall`      | Удаление C++ компонентов из системы: библиотека и CLI-бинарник (если был установлен) |
| `uninstall-app`  | Полное удаление проекта из системы, включая Go-бинарник.                             |
| `docker-image`   | Собрать Docker‑образ                                                                 |
| `docker-archive` | Архивировать образ в `$(DOCKER_ARCHIVE)`                                             |
| `docker-run`     | Запустить контейнер (используется `docker compose`)                                  |
| `docker-stop`    | Остановить контейнер                                                                 |

### 🧩 Переменные конфигурации:

| Переменная       | Назначение                       | Значение по умолчанию    |
| ---------------- | -------------------------------- | ------------------------ |
| `BUILD_SHARED`   | Собирать динамическую библиотеку | `ON`                     |
| `BUILD_STATIC`   | Собирать статическую библиотеку  | `OFF`                    |
| `INSTALL_PREFIX` | Префикс установки                | `/usr/local`             |
| `DOCKER_ARCHIVE` | Путь для архива Docker‑образа    | `docker/octet-image.tar` |

> Более подробную информацию о целях в Makefile можно получить вызовом `make help`.

---

## 📄 Лицензия

Проект распространяется под лицензией GNU General Public License v3.0.