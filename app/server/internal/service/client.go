package service

import (
	"context"
	"errors"
	"fmt"
	"net"
	"os"
	"sync"
	"time"

	guuid "github.com/google/uuid"
	"github.com/lildannita/octet-server/internal/protocol"
	"go.uber.org/zap"
)

// Конфигурация для клиента
type ClientConfig struct {
	SocketPath   string        // Путь к сокету
	ConnTimeout  time.Duration // Таймаут соединения
	ReadTimeout  time.Duration // Таймаут чтения
	WriteTimeout time.Duration // Таймаут записи
}

// Клиент для взаимодействия с C++ процессом
type Client struct {
	config ClientConfig
	conn   net.Conn
	mutex  sync.Mutex
}

// Создание нового клиента
func NewClient(config ClientConfig) (*Client, error) {
	if len(config.SocketPath) == 0 {
		return nil, errors.New("путь к сокету не указан")
	} else if _, err := os.Stat(config.SocketPath); err != nil {
		return nil, fmt.Errorf("файл сокета не найден: %w", err)
	}

	return &Client{
		config: config,
	}, nil
}

// Установка соединения с процессом octet
func (c *Client) Connect() error {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	// Если соединение уже установлено, закрываем его
	if c.conn != nil {
		c.conn.Close()
		c.conn = nil
	}

	// Устанавливаем новое соединение с таймаутом
	dialer := net.Dialer{Timeout: c.config.ConnTimeout}
	conn, err := dialer.Dial("unix", c.config.SocketPath)
	if err != nil {
		return fmt.Errorf("не удалось подключиться к сокету: %w", err)
	}

	c.conn = conn
	return nil
}

// Закрытие соединения
func (c *Client) Close() error {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	if c.conn != nil {
		err := c.conn.Close()
		c.conn = nil
		return err
	}
	return nil
}

// Проверка, установлено ли соединение
func (c *Client) IsConnected() bool {
	c.mutex.Lock()
	defer c.mutex.Unlock()
	return c.conn != nil
}

// Отправка запроса и получение ответа
func (c *Client) SendAndGet(req *protocol.Request) (*protocol.Response, error) {
	// Проверяем соединение
	if !c.IsConnected() {
		return nil, fmt.Errorf("соединение не установлено")
	}

	c.mutex.Lock()
	defer c.mutex.Unlock()

	// Устанавливаем таймаут записи
	if err := c.conn.SetWriteDeadline(time.Now().Add(c.config.WriteTimeout)); err != nil {
		return nil, fmt.Errorf("не удалось установить таймаут записи: %w", err)
	}

	// Отправляем запрос
	if err := protocol.WriteFrame(c.conn, req); err != nil {
		// Закрываем соединение при ошибке
		c.conn.Close()
		c.conn = nil
		return nil, fmt.Errorf("ошибка отправки запроса: %w", err)
	}

	// Устанавливаем таймаут чтения
	if err := c.conn.SetReadDeadline(time.Now().Add(c.config.ReadTimeout)); err != nil {
		return nil, fmt.Errorf("не удалось установить таймаут чтения: %w", err)
	}

	// Читаем ответ
	resp, err := protocol.ReadFrame(c.conn)
	if err != nil {
		// Закрываем соединение при ошибке
		c.conn.Close()
		c.conn = nil
		return nil, fmt.Errorf("ошибка чтения ответа: %w", err)
	}

	// Проверяем, что ID запроса совпадает с ID ответа
	if resp.RequestId != req.RequestId {
		return nil, fmt.Errorf("несоответствие ID запроса и ответа: %s != %s", req.RequestId, resp.RequestId)
	}

	// Если операция не успешна, возвращаем ошибку
	if !resp.Success {
		return nil, fmt.Errorf("%s", resp.Error)
	}

	return resp, nil
}

// Выполнение octet::insert
func (c *Client) Insert(ctx context.Context, data string) (string, error) {
	requestId := guuid.New().String()
	req := protocol.NewInsertRequest(requestId, data)
	resp, err := c.SendAndGet(req)
	if err != nil {
		return "", err
	}
	if resp.Params.Uuid == "" {
		return "", fmt.Errorf("получен пустой UUID в ответе")
	}
	return resp.Params.Uuid, nil
}

// Выполнение octet::get
func (c *Client) Get(ctx context.Context, uuid string) (string, error) {
	requestId := guuid.New().String()
	req := protocol.NewGetRequest(requestId, uuid)
	resp, err := c.SendAndGet(req)
	if err != nil {
		return "", err
	}
	return resp.Params.Data, nil
}

// Выполнение octet::update
func (c *Client) Update(ctx context.Context, uuid, data string) error {
	requestID := guuid.New().String()
	req := protocol.NewUpdateRequest(requestID, uuid, data)
	_, err := c.SendAndGet(req)
	return err
}

// Выполнение octet::remove
func (c *Client) Remove(ctx context.Context, uuid string) error {
	requestID := guuid.New().String()
	req := protocol.NewRemoveRequest(requestID, uuid)
	_, err := c.SendAndGet(req)
	return err
}

// Выполнение octet::ping
func (c *Client) Ping(ctx context.Context) error {
	requestID := guuid.New().String()
	req := protocol.NewPingRequest(requestID)
	_, err := c.SendAndGet(req)
	return err
}

// Конфигурация для пула клиентов
type ClientPoolConfig struct {
	SocketPath    string        // Путь к сокету
	MaxClients    int           // Максимальное количество клиентов в пуле
	ConnTimeout   time.Duration // Таймаут соединения
	ReadTimeout   time.Duration // Таймаут чтения
	WriteTimeout  time.Duration // Таймаут записи
	ClientTimeout time.Duration // Время ожидания клиента
}

// Пул клиентов, взаимодействующих с процессом octet
type ClientPool struct {
	config         ClientPoolConfig
	clients        chan *Client
	processManager *ProcessManager
}

// Создание нового пула клиентов
func NewClientPool(config ClientPoolConfig, logger *zap.Logger, pm *ProcessManager) (*ClientPool, error) {
	if config.SocketPath == "" {
		return nil, errors.New("путь к сокету не указан")
	} else if _, err := os.Stat(config.SocketPath); err != nil {
		return nil, fmt.Errorf("файл сокета не найден: %w", err)
	}

	if logger == nil {
		return nil, fmt.Errorf("внутренняя ошибка: передан пустой указать на Logger")
	}
	if pm == nil {
		return nil, fmt.Errorf("внутренняя ошибка: передан пустой указать на ProcessManager")
	}

	if config.MaxClients <= 0 {
		config.MaxClients = 10
	}
	if config.ConnTimeout == 0 {
		config.ConnTimeout = 5 * time.Second
	}
	if config.ReadTimeout == 0 {
		config.ReadTimeout = 30 * time.Second
	}
	if config.WriteTimeout == 0 {
		config.WriteTimeout = 30 * time.Second
	}

	// Создаем пул
	pool := &ClientPool{
		config:         config,
		clients:        make(chan *Client, config.MaxClients),
		processManager: pm,
	}

	// Создаем и подключаем клиентов
	for i := range config.MaxClients {
		client, err := NewClient(ClientConfig{
			SocketPath:   config.SocketPath,
			ConnTimeout:  config.ConnTimeout,
			ReadTimeout:  config.ReadTimeout,
			WriteTimeout: config.WriteTimeout,
		})
		if err != nil {
			return nil, fmt.Errorf("не удалось создать клиент %d: %w", i, err)
		}

		// Пытаемся подключиться
		if err := client.Connect(); err != nil {
			logger.Warn("Не удалось подключить клиент при инициализации, будет выполнена попытка подключения при использовании",
				zap.Int("Номер клиента", i), zap.Error(err))
		}

		// Добавляем клиент в пул
		pool.clients <- client
	}

	return pool, nil
}

// Получение клиента из пула
func (p *ClientPool) GetClient() (*PooledClient, error) {
	// Проверяем состояние процесса
	if !p.processManager.IsRunning() {
		state, exitCode, err := p.processManager.GetState()
		if state == ProcessFailed {
			return nil, fmt.Errorf("octet не запущен (код выхода: %d): %v",
				exitCode, err)
		}
		return nil, fmt.Errorf("octet не в рабочем состоянии: %v", state)
	}

	// Определяем стратегию ожидания на основе настроенного таймаута
	switch {
	case p.config.ClientTimeout < 0:
		// Ждем бесконечно, пока не освободится клиент
		client := <-p.clients
		return p.prepareClient(client)

	case p.config.ClientTimeout == 0:
		// Не ждем, сразу возвращаем ошибку если клиентов нет
		select {
		case client := <-p.clients:
			return p.prepareClient(client)
		default:
			return nil, fmt.Errorf("все клиенты заняты")
		}

	default:
		// Ждем указанное время
		select {
		case client := <-p.clients:
			return p.prepareClient(client)
		case <-time.After(p.config.ClientTimeout):
			return nil, fmt.Errorf("превышено время ожидания свободного клиента (%v)", p.config.ClientTimeout)
		}
	}
}

// Подготовка клиента к использованию
func (p *ClientPool) prepareClient(client *Client) (*PooledClient, error) {
	// Проверяем, установлено ли соединение
	if !client.IsConnected() {
		// Пытаемся подключиться
		if err := client.Connect(); err != nil {
			// Возвращаем клиент в пул и возвращаем ошибку
			p.clients <- client
			return nil, fmt.Errorf("не удалось подключить клиент: %w", err)
		}
	}

	// Возвращаем клиент, обернутый в PooledClient для автоматического возврата в пул
	return &PooledClient{
		Client: client,
		pool:   p,
		used:   false,
	}, nil
}

// Закрытие всех соединений и освобождение ресурсов
func (p *ClientPool) Close() {
	// Закрываем все клиенты
	clientsCount := len(p.clients)
	for i := 0; i < clientsCount; i++ {
		select {
		case client := <-p.clients:
			client.Close()
		default:
			// Если канал пуст, выходим
			return
		}
	}

	// Закрываем канал
	close(p.clients)
}

// Обертка для клиента для автоматического возрата в пул
type PooledClient struct {
	*Client
	pool *ClientPool
	used bool
}

// Возврат клиента в пул
func (pc *PooledClient) Release() {
	if pc.used {
		return
	}
	pc.used = true
	pc.pool.clients <- pc.Client
}

// Выполнение octet::insert и возврат клиента в пул
func (pc *PooledClient) Insert(ctx context.Context, data string) (string, error) {
	defer pc.Release()
	return pc.Client.Insert(ctx, data)
}

// Выполнение octet::get и возврат клиента в пул
func (pc *PooledClient) Get(ctx context.Context, uuid string) (string, error) {
	defer pc.Release()
	return pc.Client.Get(ctx, uuid)
}

// Выполнение octet::update и возврат клиента в пул
func (pc *PooledClient) Update(ctx context.Context, uuid, data string) error {
	defer pc.Release()
	return pc.Client.Update(ctx, uuid, data)
}

// Выполнение octet::remove и возврат клиента в пул
func (pc *PooledClient) Remove(ctx context.Context, uuid string) error {
	defer pc.Release()
	return pc.Client.Remove(ctx, uuid)
}

// Выполнение octet::ping и возврат клиента в пул
func (pc *PooledClient) Ping(ctx context.Context) error {
	defer pc.Release()
	return pc.Client.Ping(ctx)
}
