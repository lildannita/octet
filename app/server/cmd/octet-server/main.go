package main

import (
	"context"
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/lildannita/octet-server/internal/api"
	"github.com/lildannita/octet-server/internal/config"
	"github.com/lildannita/octet-server/internal/service"
	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"
)

func main() {
	// Парсинг аргументов командной строки
	configPath := flag.String("config", "", "Путь к файлу конфигурации")
	logLevel := flag.String("log-level", "info", "Уровень логирования (debug, info, warn, error)")
	flag.Parse()

	// Инициализация логгера
	logConfig := zap.NewProductionConfig()
	logConfig.DisableStacktrace = true
	switch *logLevel {
	case "debug":
		logConfig.Level.SetLevel(zapcore.DebugLevel)
		logConfig.DisableStacktrace = false
	case "info":
		logConfig.Level.SetLevel(zapcore.InfoLevel)
	case "warn":
		logConfig.Level.SetLevel(zapcore.WarnLevel)
	case "error":
		logConfig.Level.SetLevel(zapcore.ErrorLevel)
	default:
		logConfig.Level.SetLevel(zapcore.InfoLevel)
	}
	logger, err := logConfig.Build()
	if err != nil {
		log.Fatalf("Ошибка инициализации логгера: %v", err)
	}
	defer logger.Sync()
	zap.ReplaceGlobals(logger)

	// Загрузка конфигурации
	cfg, err := config.Load(*configPath)
	if err != nil {
		logger.Fatal("Ошибка загрузки конфигурации", zap.Error(err))
	}

	// Создание и запуск процесса octet
	procManager := service.NewProcessManager(cfg)
	if err := procManager.Start(); err != nil {
		logger.Fatal("Не удалось запустить процесс octet", zap.Error(err))
	}
	defer procManager.Stop()

	// Создание клиентского пула соединений
	clientPool, err := service.NewClientPool(service.ClientPoolConfig{
		SocketPath:    cfg.SocketPath,
		MaxClients:    cfg.MaxClients,
		ConnTimeout:   5 * time.Second,
		ReadTimeout:   30 * time.Second,
		WriteTimeout:  30 * time.Second,
		ClientTimeout: 30 * time.Second,
	}, logger, procManager)
	if err != nil {
		logger.Fatal("Не удалось создать пул клиентов", zap.Error(err))
	}
	defer clientPool.Close()

	// Создание REST API сервера
	router := api.NewRouter(api.RouterConfig{
		ClientPool: clientPool,
		Logger:     logger,
	})
	server := &http.Server{
		Addr:         cfg.HTTPAddr,
		Handler:      router,
		ReadTimeout:  60 * time.Second,
		WriteTimeout: 60 * time.Second,
		IdleTimeout:  120 * time.Second,
	}

	// Запуск HTTP сервера в отдельной горутине
	go func() {
		logger.Info("Запуск HTTP сервера", zap.String("addr", cfg.HTTPAddr))
		if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			logger.Fatal("Ошибка при запуске HTTP сервера", zap.Error(err))
		}
	}()

	// Ожидание сигнала для корректного завершения
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	sig := <-sigChan
	logger.Info("Получен сигнал завершения", zap.String("signal", sig.String()))

	// Корректное завершение сервера с таймаутом 30 секунд
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	if err := server.Shutdown(ctx); err != nil {
		logger.Error("Ошибка при корректном завершении HTTP сервера", zap.Error(err))
	}

	logger.Info("Сервер успешно завершил работу")
}
