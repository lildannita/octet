package config

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

// Экспортируемая переменная, которую можно задать при компиляции
var OctetPath string

// Config содержит все конфигурационные параметры приложения
type Config struct {
	StorageDir string `json:"storage_dir"` // Путь к директории хранилища данных
	SocketPath string `json:"socket_path"` // Путь к UNIX domain socket для связи с C++ процессом
	OctetPath  string `json:"octet_path"`  // Путь к исполняемому файлу octet
	HTTPAddr   string `json:"http_addr"`   // Адрес и порт для HTTP сервера
	MaxClients int    `json:"max_clients"` // Максимальное количество клиентов
}

// Загрузка конфигурации из JSON файла по указанному пути
func loadFromFile(path string, config *Config) error {
	// Проверяем существование файла
	if _, err := os.Stat(path); os.IsNotExist(err) {
		return fmt.Errorf("файл конфигурации не найден: %s", path)
	}

	// Читаем файл
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("не удалось прочитать файл конфигурации: %w", err)
	}

	// Разбираем JSON
	if err := json.Unmarshal(data, config); err != nil {
		return fmt.Errorf("не удалось разобрать файл конфигурации: %w", err)
	}

	return nil
}

// Load загружает конфигурацию из файла и командной строки
func Load(configPath string) (*Config, error) {
	// Создаем дефолтный конфиг
	config := &Config{
		StorageDir: filepath.Join(os.TempDir(), "octet-storage"),
		SocketPath: filepath.Join(os.TempDir(), "octet.sock"),
		OctetPath:  "octet",
		HTTPAddr:   ":8080",
	}

	// Если указан путь к файлу конфигурации, загружаем из него
	if configPath != "" {
		if err := loadFromFile(configPath, config); err != nil {
			return nil, err
		}
	}

	// Если путь к octet задан при компиляции, то он будет в приоритете
	if OctetPath != "" {
		config.OctetPath = OctetPath
	}

	// Проверяем обязательные параметры
	if config.StorageDir == "" {
		return nil, fmt.Errorf("путь к директории с хранилищем не указан")
	}
	if config.OctetPath == "" {
		return nil, fmt.Errorf("путь к исполняемому файлу octet не указан")
	} else if _, err := os.Stat(config.OctetPath); err != nil {
		return nil, fmt.Errorf("исполняемый файл octet не найден: %w", err)
	}

	return config, nil
}
