#!/bin/bash
set -e

# Функция для изменения UID/GID пользователя
adjust_user_permissions() {
    local target_uid=$1
    local target_gid=$2
    
    if [ "$(id -u octet)" != "$target_uid" ] || [ "$(id -g octet)" != "$target_gid" ]; then
        echo "Adjusting octet user to UID=$target_uid, GID=$target_gid"
        
        # Сохраняем старые UID/GID
        local old_uid=$(id -u octet)
        local old_gid=$(id -g octet)
        
        # Изменяем UID и GID
        groupmod -o -g "$target_gid" octet 2>/dev/null || true
        usermod -o -u "$target_uid" -g "$target_gid" octet 2>/dev/null || true
        
        # Рекурсивно меняем владельца домашней директории
        echo "Updating file ownership from $old_uid:$old_gid to $target_uid:$target_gid"
        chown -R octet:octet /home/octet
    fi
}

if [ "$(id -u)" = "0" ]; then
    # Если контейнер запущен от root

    # Проверяем существование и права директории /storage
    if [ -d "/storage" ]; then
        # Получаем UID и GID владельца директории
        STORAGE_UID=$(stat -c '%u' /storage 2>/dev/null || stat -f '%u' /storage)
        STORAGE_GID=$(stat -c '%g' /storage 2>/dev/null || stat -f '%g' /storage)
        
        # Если директория принадлежит root, но монтирована с хоста
        if [ "$STORAGE_UID" = "0" ] && [ -n "$HOST_UID" ] && [ -n "$HOST_GID" ]; then
            # Используем переданные UID/GID с хоста
            adjust_user_permissions "$HOST_UID" "$HOST_GID"
            chown -R "$HOST_UID:$HOST_GID" /storage
        else
            # Подстраиваемся под существующего владельца
            adjust_user_permissions "$STORAGE_UID" "$STORAGE_GID"
        fi
    else
        # Если директория не существует, создаем её
        mkdir -p /storage
        if [ -n "$HOST_UID" ] && [ -n "$HOST_GID" ]; then
            adjust_user_permissions "$HOST_UID" "$HOST_GID"
            chown -R "$HOST_UID:$HOST_GID" /storage
        else
            chown -R octet:octet /storage
        fi
    fi
    
    # Запускаем приложение от имени пользователя octet
    exec gosu octet octet-server --config=/home/octet/config.json
else
    # Если не root, просто запускаем приложение
    exec octet-server --config=/home/octet/config.json
fi
