package api

import (
	"net/http"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/go-chi/chi/v5/middleware"
	"github.com/go-chi/cors"
	"github.com/lildannita/octet-server/internal/service"
	"go.uber.org/zap"
)

// RouterConfig содержит конфигурацию для роутера
type RouterConfig struct {
	// Пул клиентов для взаимодействия с C++ процессом
	ClientPool *service.ClientPool
	// Логгер
	Logger *zap.Logger
}

// NewRouter создает новый роутер с настроенными маршрутами
func NewRouter(config RouterConfig) http.Handler {
	if config.ClientPool == nil {
		panic("пул клиентов не указан")
	}
	if config.Logger == nil {
		panic("логгер не указан")
	}

	r := chi.NewRouter()

	// Базовые middleware
	r.Use(middleware.RequestID)
	r.Use(middleware.RealIP)
	r.Use(middleware.Recoverer)
	r.Use(middleware.Timeout(60 * time.Second))
	r.Use(LoggerMiddleware(config.Logger))
	// CORS
	r.Use(cors.Handler(cors.Options{
		AllowedOrigins:   []string{"*"},
		AllowedMethods:   []string{"GET", "POST", "PUT", "DELETE", "OPTIONS"},
		AllowedHeaders:   []string{"Accept", "Content-Type"},
		ExposedHeaders:   []string{"Link"},
		AllowCredentials: false,
		MaxAge:           300,
	}))
	// Проверка Content-Type middleware
	r.Use(ContentTypeMiddleware("application/json"))

	// Обработчики API
	h := &Handler{
		clientPool: config.ClientPool,
		logger:     config.Logger,
	}

	// Маршруты
	r.Get("/health", h.HealthCheck)

	// API
	r.Route("/octet", func(r chi.Router) {
		// API v1
		r.Route("/v1", func(r chi.Router) {
			r.Post("/", h.Insert)
			r.Get("/{uuid}", h.Get)
			r.Put("/{uuid}", h.Update)
			r.Delete("/{uuid}", h.Remove)
		})
	})

	return r
}
