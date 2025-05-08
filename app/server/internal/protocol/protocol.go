package protocol

import (
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
)

type CommandType string

// Команды, которые могут быть переданы C++ процессу
const (
	CommandInsert CommandType = "insert"
	CommandGet    CommandType = "get"
	CommandUpdate CommandType = "update"
	CommandRemove CommandType = "remove"
	CommandPing   CommandType = "ping"
)

// Request представляет запрос к C++ процессу
type Request struct {
	RequestId string           `json:"request_id"`
	Command   CommandType      `json:"command"`
	Params    AdditionalParams `json:"params"`
}

// Response представляет ответ от C++ процесса
type Response struct {
	RequestId string           `json:"request_id"`
	Success   bool             `json:"success"`
	Params    AdditionalParams `json:"params"`
	Error     string           `json:"error,omitempty"`
}

// AdditionalParams содержит дополнительные данные для Request/Response
type AdditionalParams struct {
	Uuid string `json:"uuid,omitempty"`
	Data string `json:"data,omitempty"`
}

// Длина заголовка сообщения - 4 байта
// (т.к. в качестве заголовока используем длину сообщения типом uint32)
const headerSize = 4

// Сериализация запроса в бинарный формат
func Encode(request *Request) ([]byte, error) {
	// Сериализуем запрос в JSON
	jsonData, err := json.Marshal(request)
	if err != nil {
		return nil, fmt.Errorf("ошибка сериализации запроса: %w", err)
	}

	// Подготавливаем результирующий буфер
	// [4 байта длины сообщения][JSON-сообщение]
	messageLength := uint32(len(jsonData))
	result := make([]byte, headerSize+len(jsonData))

	// Записываем длину сообщения (в формате little endian)
	binary.LittleEndian.PutUint32(result[:headerSize], messageLength)

	// Копируем JSON-данные
	copy(result[headerSize:], jsonData)

	return result, nil
}

// Десериализация ответа из бинарного формата
func Decode(data []byte) (*Response, error) {
	if len(data) < headerSize {
		return nil, errors.New("недостаточно данных для чтения заголовка")
	}

	// Читаем длину сообщения
	messageLength := binary.LittleEndian.Uint32(data[:headerSize])

	// Проверяем, что у нас достаточно данных
	if len(data) < headerSize+int(messageLength) {
		return nil, errors.New("недостаточно данных для чтения сообщения")
	}

	// Читаем JSON-данные
	jsonData := data[headerSize : headerSize+messageLength]

	// Десериализуем JSON
	var response Response
	if err := json.Unmarshal(jsonData, &response); err != nil {
		return nil, fmt.Errorf("ошибка десериализации ответа: %w", err)
	}

	return &response, nil
}

// Чтение одного фрейма из Reader
func ReadFrame(reader io.Reader) (*Response, error) {
	// Чтение длины сообщения
	lengthBuf := make([]byte, 4)
	if _, err := io.ReadFull(reader, lengthBuf); err != nil {
		return nil, fmt.Errorf("ошибка чтения длины фрейма: %w", err)
	}

	// Декодирование длины сообщения
	messageLength := binary.LittleEndian.Uint32(lengthBuf)

	// Чтение сообщения
	messageBuf := make([]byte, messageLength)
	if _, err := io.ReadFull(reader, messageBuf); err != nil {
		return nil, fmt.Errorf("ошибка чтения данных фрейма: %w", err)
	}

	// Десериализация JSON-ответа
	var response Response
	if err := json.Unmarshal(messageBuf, &response); err != nil {
		return nil, fmt.Errorf("ошибка десериализации ответа: %w", err)
	}

	return &response, nil
}

// Запись одного фрейма в Writer
func WriteFrame(writer io.Writer, request *Request) error {
	// Сериализация запроса в бинарный формат
	data, err := Encode(request)
	if err != nil {
		return err
	}

	// Запись всех данных
	if _, err := writer.Write(data); err != nil {
		return fmt.Errorf("ошибка записи фрейма: %w", err)
	}

	return nil
}

// Создание нового запроса добавления данных
func NewInsertRequest(requestId, data string) *Request {
	return &Request{
		RequestId: requestId,
		Command:   CommandInsert,
		Params: AdditionalParams{
			Data: data,
		},
	}
}

// Создание нового запроса получения данных
func NewGetRequest(requestId, uuid string) *Request {
	return &Request{
		RequestId: requestId,
		Command:   CommandGet,
		Params: AdditionalParams{
			Uuid: uuid,
		},
	}
}

// Создание нового запроса обновления данных
func NewUpdateRequest(requestId, uuid, data string) *Request {
	return &Request{
		RequestId: requestId,
		Command:   CommandUpdate,
		Params: AdditionalParams{
			Uuid: uuid,
			Data: data,
		},
	}
}

// Создание нового запроса удаления данных
func NewRemoveRequest(requestId, uuid string) *Request {
	return &Request{
		RequestId: requestId,
		Command:   CommandRemove,
		Params: AdditionalParams{
			Uuid: uuid,
		},
	}
}

// Создание нового запроса удаления данных
func NewPingRequest(requestId string) *Request {
	return &Request{
		RequestId: requestId,
		Command:   CommandPing,
	}
}
