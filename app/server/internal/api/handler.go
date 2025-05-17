package api

import (
	"encoding/json"
	"net/http"
	"time"

	"github.com/go-chi/chi/v5"
	"github.com/lildannita/octet-server/internal/service"
	"go.uber.org/zap"
)

// Для получения/отправки строки хранилища
type DataHeader struct {
	Data string `json:"data"`
}

// Для отправки UUID строки
type UuidHeader struct {
	Uuid string `json:"uuid"`
}

// Для ответа с информацией об ошибке
type ErrorHeader struct {
	Error string `json:"error"`
}

// Ответ на запрос проверки работоспособности
type HealthCheckResponse struct {
	Status    string `json:"status"`
	Timestamp string `json:"timestamp"`
}

// Handler содержит обработчики HTTP-запросов
type Handler struct {
	clientPool *service.ClientPool
	logger     *zap.Logger
}

// HealthCheck godoc
// @Summary Проверка работоспособности
// @Description Проверка, работает ли сервис и менеджер хранилища
// @Tags health
// @Produce json
// @Success 200 {object} HealthCheckResponse
// @Router /health [get]
func (h *Handler) HealthCheck(w http.ResponseWriter, r *http.Request) {
	// Получаем клиент из пула
	client, err := h.clientPool.GetClient()
	if err != nil {
		h.logger.Error("Не удалось получить клиент из пула", zap.Error(err))
		respondWithError(w, http.StatusInternalServerError, "Сервер недоступен")
		return
	}

	// Проверяем подключение к octet
	if err := client.Ping(r.Context()); err != nil {
		h.logger.Error("Не удалось выполнить octet::ping", zap.Error(err))
		respondWithError(w, http.StatusInternalServerError, "Сервер недоступен")
		return
	}

	// Все хорошо, отправляем ответ
	respondWithJSON(w, http.StatusOK, HealthCheckResponse{
		Status:    "ok",
		Timestamp: time.Now().Format(time.RFC3339),
	})
}

// Insert godoc
// @Summary Добавление новой строки
// @Description Сохранение строки UTF-8 и получение UUID
// @Tags strings
// @Accept json
// @Produce json
// @Param data body DataHeader true "Строка для сохранения"
// @Success 201 {object} UuidHeader
// @Failure 400 {object} ErrorHeader
// @Failure 500 {object} ErrorHeader
// @Router /octet/v1 [post]
func (h *Handler) Insert(w http.ResponseWriter, r *http.Request) {
	// Разбираем запрос
	var insertReq DataHeader
	if err := json.NewDecoder(r.Body).Decode(&insertReq); err != nil {
		h.logger.Error("Ошибка при разборе запроса", zap.Error(err))
		respondWithError(w, http.StatusBadRequest, "Некорректный запрос")
		return
	}

	// Проверяем данные
	if len(insertReq.Data) == 0 {
		respondWithError(w, http.StatusBadRequest, "Поле 'data' не может быть пустым")
		return
	}

	// Получаем клиент из пула
	client, err := h.clientPool.GetClient()
	if err != nil {
		h.logger.Error("Не удалось получить клиент из пула", zap.Error(err))
		respondWithError(w, http.StatusInternalServerError, "Внутренняя ошибка сервера")
		return
	}

	// Отправляем запрос на создание строки
	uuid, err := client.Insert(r.Context(), insertReq.Data)
	if err != nil {
		h.logger.Error("Ошибка при добавлении данных", zap.Error(err))
		respondWithError(w, http.StatusInternalServerError, "Ошибка при добавлении данных: "+err.Error())
		return
	}

	// Отправляем ответ
	respondWithJSON(w, http.StatusCreated, UuidHeader{Uuid: uuid})
}

// Get godoc
// @Summary Получение строки по UUID
// @Description Извлечение строки из хранилища по её UUID
// @Tags strings
// @Produce json
// @Param uuid path string true "UUID строки"
// @Success 200 {object} DataHeader
// @Failure 400 {object} ErrorHeader
// @Failure 500 {object} ErrorHeader
// @Router /octet/v1/{uuid} [get]
func (h *Handler) Get(w http.ResponseWriter, r *http.Request) {
	// Получаем UUID из URL
	uuid := chi.URLParam(r, "uuid")
	if len(uuid) == 0 {
		respondWithError(w, http.StatusBadRequest, "UUID не указан")
		return
	}

	// Получаем клиент из пула
	client, err := h.clientPool.GetClient()
	if err != nil {
		h.logger.Error("Не удалось получить клиент из пула", zap.Error(err))
		respondWithError(w, http.StatusInternalServerError, "Внутренняя ошибка сервера")
		return
	}

	// Получаем строку
	data, err := client.Get(r.Context(), uuid)
	if err != nil {
		h.logger.Error("Ошибка при получении строки", zap.Error(err))
		respondWithError(w, http.StatusInternalServerError, "Ошибка при получении строки: "+err.Error())
		return
	}

	// Отправляем ответ
	respondWithJSON(w, http.StatusOK, DataHeader{Data: data})
}

// Update godoc
// @Summary Обновление существующей строки
// @Description Обновление строки по её UUID
// @Tags strings
// @Accept json
// @Produce json
// @Param uuid path string true "UUID строки"
// @Param data body DataHeader true "Новое значение строки"
// @Success 204
// @Failure 400 {object} ErrorHeader
// @Failure 500 {object} ErrorHeader
// @Router /octet/v1/{uuid} [put]
func (h *Handler) Update(w http.ResponseWriter, r *http.Request) {
	// Получаем UUID из URL
	uuid := chi.URLParam(r, "uuid")
	if len(uuid) == 0 {
		respondWithError(w, http.StatusBadRequest, "UUID не указан")
		return
	}

	// Разбираем запрос
	var updateReq DataHeader
	if err := json.NewDecoder(r.Body).Decode(&updateReq); err != nil {
		h.logger.Error("Ошибка при разборе запроса", zap.Error(err))
		respondWithError(w, http.StatusBadRequest, "Некорректный запрос")
		return
	}

	// Проверяем данные
	if len(updateReq.Data) == 0 {
		respondWithError(w, http.StatusBadRequest, "Поле 'data' не может быть пустым")
		return
	}

	// Получаем клиент из пула
	client, err := h.clientPool.GetClient()
	if err != nil {
		h.logger.Error("Не удалось получить клиент из пула", zap.Error(err))
		respondWithError(w, http.StatusInternalServerError, "Внутренняя ошибка сервера")
		return
	}

	// Обновляем строку
	if err := client.Update(r.Context(), uuid, updateReq.Data); err != nil {
		h.logger.Error("Ошибка при обновлении строки", zap.Error(err))
		respondWithError(w, http.StatusInternalServerError, "Ошибка при обновлении строки: "+err.Error())
		return
	}

	// Отправляем ответ
	w.WriteHeader(http.StatusNoContent)
}

// Remove godoc
// @Summary Удаление строки
// @Description Удаление строки по её UUID
// @Tags strings
// @Param uuid path string true "UUID строки"
// @Success 204
// @Failure 400 {object} ErrorHeader
// @Failure 500 {object} ErrorHeader
// @Router /octet/v1/{uuid} [delete]
func (h *Handler) Remove(w http.ResponseWriter, r *http.Request) {
	// Получаем UUID из URL
	uuid := chi.URLParam(r, "uuid")
	if len(uuid) == 0 {
		respondWithError(w, http.StatusBadRequest, "UUID не указан")
		return
	}

	// Получаем клиент из пула
	client, err := h.clientPool.GetClient()
	if err != nil {
		h.logger.Error("Не удалось получить клиент из пула", zap.Error(err))
		respondWithError(w, http.StatusInternalServerError, "Внутренняя ошибка сервера")
		return
	}

	// Удаляем строку
	if err := client.Remove(r.Context(), uuid); err != nil {
		h.logger.Error("Ошибка при удалении строки", zap.Error(err))
		respondWithError(w, http.StatusInternalServerError, "Ошибка при удалении строки: "+err.Error())
		return
	}

	// Отправляем ответ (204 No Content)
	w.WriteHeader(http.StatusNoContent)
}

// respondWithError отправляет клиенту ответ с ошибкой
func respondWithError(w http.ResponseWriter, code int, message string) {
	respondWithJSON(w, code, ErrorHeader{Error: message})
}

// respondWithJSON отправляет клиенту ответ в формате JSON
func respondWithJSON(w http.ResponseWriter, code int, payload interface{}) {
	response, err := json.Marshal(payload)
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		w.Write([]byte(`{"error":"Ошибка при формировании ответа"}`))
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	w.Write(response)
}
