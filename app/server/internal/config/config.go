package config

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
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
	var homePath string
	var octetDir string
	if homeDir, err := os.UserHomeDir(); err == nil {
		homePath = homeDir
		octetDir = filepath.Join(homePath, "octet")
	} else {
		return nil, fmt.Errorf("не удалось получить путь к домашней директории: %w", err)
	}

	// Создаем дефолтный конфиг
	config := &Config{
		StorageDir: filepath.Join(octetDir, "storage"),
		SocketPath: filepath.Join(octetDir, "octet.sock"),
		OctetPath:  "",
		HTTPAddr:   ":8080",
	}

	var baseDir string
	if len(configPath) != 0 {
		// Используем абсолютные пути относительно директории конфига
		absCfgPath, err := filepath.Abs(configPath)
		if err != nil {
			return nil, fmt.Errorf("не удалось получить абсолютный путь к файлу конфигурации: %w", err)
		}
		// Если указан путь к файлу конфигурации, загружаем из него
		if err := loadFromFile(absCfgPath, config); err != nil {
			return nil, err
		}
		baseDir = filepath.Dir(absCfgPath)
	}

	resolve := func(p string) string {
		if len(p) == 0 || filepath.IsAbs(p) || len(baseDir) == 0 {
			return p
		}
		if strings.HasPrefix(p, "~") {
			// Разрешаем путь содержащий домашнюю директорию
			if len(p) > 2 {
				return filepath.Join(homePath, p[2:])
			} else {
				return homePath
			}
		} else {
			// Разрешаем путь, относительно директории, в которой лежит конфиг
			return filepath.Clean(filepath.Join(baseDir, p))
		}

	}
	config.StorageDir = resolve(config.StorageDir)
	config.SocketPath = resolve(config.SocketPath)

	config.OctetPath = resolve(config.OctetPath)
	// Если путь к octet не задан в конфиге, то используем путь, указанный при компиляции
	if len(config.OctetPath) == 0 {
		config.OctetPath = OctetPath
	}

	// Проверяем обязательные параметры
	if len(config.StorageDir) == 0 {
		return nil, fmt.Errorf("путь к директории с хранилищем не указан")
	}
	if len(config.OctetPath) == 0 {
		return nil, fmt.Errorf("путь к исполняемому файлу octet не указан")
	} else if _, err := os.Stat(config.OctetPath); err != nil {
		return nil, fmt.Errorf("исполняемый файл octet не найден: %w", err)
	}

	return config, nil
}
