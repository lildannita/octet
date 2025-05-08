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

// Проверка работоспособности сервера
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

// Добавление строки в хранилище (POST [JSON] -> UuidHeader/ErrorHeader)
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

// Получение строки из хранилища (GET [URL] -> DataHeader/ErrorHeader)
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

// Обновление строки в хранилище (PUT [URL+JSON] -> 204/ErrorHeader)
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

// Удаление строки из хранилища (DELETE [URL] -> 204/ErrorHeader)
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
