# Этап сборки
FROM debian:bullseye-slim AS builder

# Установка зависимостей для сборки
RUN apt-get update \ 
        && apt-get install -y --no-install-recommends \ 
            wget \ 
            ca-certificates \ 
            tar \ 
            build-essential \ 
            cmake \ 
            ninja-build \ 
            pkg-config \ 
            libboost-system-dev \ 
        && rm -rf /var/lib/apt/lists/*

# Установка Go нужной версии (у Debian слишком старая версия)
ARG GO_VERSION=1.24.2
RUN ARCH=$(dpkg --print-architecture) \
    && case ${ARCH} in \
       amd64) GO_ARCH='amd64' ;; \
       arm64) GO_ARCH='arm64' ;; \
       *) echo "Unsupported architecture: ${ARCH}" && exit 1 ;; \
    esac \
    && wget --no-check-certificate "https://go.dev/dl/go${GO_VERSION}.linux-${GO_ARCH}.tar.gz" \
    && tar -C /usr/local -xzf "go${GO_VERSION}.linux-${GO_ARCH}.tar.gz" \
    && rm "go${GO_VERSION}.linux-${GO_ARCH}.tar.gz"
# Добавление Go в PATH
ENV PATH=$PATH:/usr/local/go/bin
ENV GOPATH=/go
ENV PATH=$PATH:$GOPATH/bin
# Проверка версии Go
RUN go version

# Создание директории для исходного кода
WORKDIR /octet-source

# Копирование исходного кода проекта
COPY api/ api/
COPY app/ app/
COPY cmake/ cmake/
COPY include/ include/
COPY src/ src/
COPY CMakeLists.txt Makefile ./

# Собираем проект
RUN make docker-build

# Проверяем, что сборка прошла успешно
RUN ls -la build/bin/

# Финальный образ
FROM debian:bullseye-slim

# Нужно только доустановить curl для Healthcheck
RUN apt-get update \ 
        && apt-get install -y curl \ 
        && rm -rf /var/lib/apt/lists/*

# Создаем пользователя octet
RUN useradd -m -d /home/octet octet

# Создаем директорию для хранилища и настраиваем права
RUN mkdir -p /storage \ 
    && chown -R octet:octet /storage \ 
    && chmod -R 755 /storage

# Копируем бинарные файлы из предыдущего этапа
COPY --from=builder /octet-source/build/bin/octet /usr/local/bin/
COPY --from=builder /octet-source/build/bin/octet-server /usr/local/bin/

# Копируем файл конфигурации
COPY docker/config.json /home/octet/config.json
RUN chown octet:octet /home/octet/config.json

# Объявляем том для монтирования директории хранилища
VOLUME /storage

# Открываем порт для API
EXPOSE 8080

# Переключаемся на пользователя octet
USER octet
WORKDIR /home/octet

# Запускаем Go-сервер с указанным конфигурационным файлом
ENTRYPOINT ["octet-server", "--config=/home/octet/config.json"]
