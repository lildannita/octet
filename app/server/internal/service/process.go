package service

import (
	"fmt"
	"os"
	"os/exec"
	"sync"
	"syscall"
	"time"

	"github.com/lildannita/octet-server/internal/config"
	"go.uber.org/zap"
)

// ProcessState представляет текущее состояние C++ процесса
type ProcessState int

const (
	ProcessNotStarted ProcessState = iota // Процесс еще не был запущен
	ProcessRunning                        // Процесс запущен и работает
	ProcessStopped                        // Процесс остановлен намеренно
	ProcessFailed                         // Процесс завершился с ошибкой
)

// Структура управления процессом octet
type ProcessManager struct {
	config       *config.Config
	cmd          *exec.Cmd
	logger       *zap.Logger
	mutex        sync.Mutex
	state        ProcessState
	exitCode     int
	exitError    error
	stateChanged chan struct{}
}

// Создание нового ProcessManager
func NewProcessManager(config *config.Config) *ProcessManager {
	return &ProcessManager{
		config:       config,
		logger:       zap.NewNop(),
		state:        ProcessNotStarted,
		stateChanged: make(chan struct{}, 1),
	}
}

// Запуск процесса octet
func (pm *ProcessManager) Start() error {
	pm.mutex.Lock()

	if pm.state == ProcessRunning {
		pm.mutex.Unlock()
		return fmt.Errorf("процесс уже запущен")
	}

	pm.logger.Info("Запуск процесса octet",
		zap.String("octet", pm.config.OctetPath),
		zap.String("storage", pm.config.StorageDir),
		zap.String("socket", pm.config.SocketPath))

	// Проверяем, что исполняемый файл существует
	if _, err := os.Stat(pm.config.OctetPath); err != nil {
		pm.mutex.Unlock()
		pm.changeState(ProcessFailed)
		return fmt.Errorf("исполняемый файл не найден: %w", err)
	}

	// Проверяем, существует ли файл сокета
	if _, err := os.Stat(pm.config.SocketPath); err == nil {
		pm.logger.Warn("Файл сокета уже существует, удаляем его", zap.String("socket", pm.config.SocketPath))
		// Если существует, то пытаемся удалить его
		if err := os.Remove(pm.config.SocketPath); err != nil {
			pm.mutex.Unlock()
			pm.changeState(ProcessFailed)
			return fmt.Errorf("не удалось удалить существующий файл сокета: %w", err)
		}
	}

	// Создаем команду для запуска процесса
	pm.cmd = exec.Command(
		pm.config.OctetPath,
		"--storage="+pm.config.StorageDir,
		"--server",
		"--socket="+pm.config.SocketPath,
	)

	// Настраиваем перенаправление stdout и stderr
	pm.cmd.Stdout = os.Stdout
	pm.cmd.Stderr = os.Stderr

	// Запускаем процесс
	if err := pm.cmd.Start(); err != nil {
		pm.mutex.Unlock()
		pm.changeState(ProcessFailed)
		return fmt.Errorf("не удалось запустить процесс: %w", err)
	}

	// Ждем создания файла сокета (проверяем каждые 100 мс на протяжении 10 секунд)
	socketExists := false
	for attempt := 0; attempt < 100; attempt++ {
		time.Sleep(100 * time.Millisecond)
		if _, err := os.Stat(pm.config.SocketPath); err != nil {
			continue
		}
		socketExists = true
		break
	}

	if !socketExists {
		// Пытаемся убить процесс, если он не смог создать сокет
		pm.cmd.Process.Kill()

		// Проверяем, не завершился ли процесс
		if pm.cmd.ProcessState != nil && pm.cmd.ProcessState.Exited() {
			pm.mutex.Unlock()
			pm.changeState(ProcessFailed)
			return fmt.Errorf("процесс завершился преждевременно с кодом %d", pm.cmd.ProcessState.ExitCode())
		}

		pm.mutex.Unlock()
		pm.changeState(ProcessFailed)
		return fmt.Errorf("файл сокета не был создан в течение таймаута")
	}

	pm.mutex.Unlock()
	pm.changeState(ProcessRunning)
	go pm.monitorProcess()

	return nil
}

// Остановка процесса octet
func (pm *ProcessManager) Stop() error {
	pm.mutex.Lock()

	if pm.state != ProcessRunning || pm.cmd == nil {
		pm.mutex.Unlock()
		return fmt.Errorf("процесс octet не запущен")
	}

	pm.logger.Info("Остановка процесса octet")

	// Пытаемся корректно завершить процесс
	if err := pm.cmd.Process.Signal(syscall.SIGTERM); err != nil {
		pm.logger.Warn("Не удалось отправить сигнал SIGTERM, пытаемся убить процесс", zap.Error(err))
		// Если не удалось отправить SIGTERM, убиваем процесс
		if err := pm.cmd.Process.Kill(); err != nil {
			pm.mutex.Unlock()
			pm.changeState(ProcessFailed)
			return fmt.Errorf("не удалось завершить процесс: %w", err)
		}
	}

	// Ждем завершения процесса с таймаутом
	done := make(chan error, 1)
	go func() {
		done <- pm.cmd.Wait()
	}()

	var err error
	select {
	case err = <-done:
		if err != nil {
			pm.logger.Warn("Процесс завершился с ошибкой", zap.Error(err))
		} else {
			pm.logger.Info("Процесс успешно завершился")
		}
	case <-time.After(10 * time.Second):
		// Если процесс не завершился за 5 секунд, убиваем его
		pm.logger.Warn("Таймаут ожидания завершения процесса, принудительное завершение")
		if err := pm.cmd.Process.Kill(); err != nil {
			pm.mutex.Unlock()
			pm.changeState(ProcessFailed)
			return fmt.Errorf("не удалось принудительно завершить процесс: %w", err)
		}

		err = <-done
		if err != nil {
			pm.logger.Warn("Процесс завершился с ошибкой после принудительного завершения",
				zap.Error(err))
		}
	}

	pm.mutex.Unlock()
	pm.changeState(ProcessStopped)
	pm.cmd = nil

	return nil
}

// Геттер для текущего состояния процесса
func (pm *ProcessManager) GetState() (ProcessState, int, error) {
	pm.mutex.Lock()
	defer pm.mutex.Unlock()

	return pm.state, pm.exitCode, pm.exitError
}

// Проверка, запущен ли процесс
func (pm *ProcessManager) IsRunning() bool {
	pm.mutex.Lock()
	defer pm.mutex.Unlock()

	return pm.state == ProcessRunning
}

// Ожидание изменения состояния процесса с таймаутом
func (pm *ProcessManager) WaitForStateChange(timeout time.Duration) bool {
	select {
	case <-pm.stateChanged:
		return true
	case <-time.After(timeout):
		return false
	}
}

// Отслеживание работы процесса
func (pm *ProcessManager) monitorProcess() {
	if pm.cmd == nil {
		return
	}

	// Ждем завершения процесса
	err := pm.cmd.Wait()

	// Если процесс завершился, обновляем состояние
	pm.mutex.Lock()

	// Проверяем, был ли процесс остановлен намеренно
	if pm.state == ProcessStopped {
		pm.mutex.Unlock()
		return
	}

	exitCode := 0
	if pm.cmd.ProcessState != nil {
		exitCode = pm.cmd.ProcessState.ExitCode()
	}
	pm.exitCode = exitCode
	pm.exitError = err

	if err != nil {
		pm.logger.Error("Процесс octet завершился с ошибкой",
			zap.Error(err),
			zap.Int("Код завершения", exitCode))
	} else {
		pm.logger.Error("Процесс octet неожиданно завершился",
			zap.Int("Код завершения", exitCode))
	}

	pm.mutex.Unlock()

	// Изменяем состояние
	pm.changeState(ProcessFailed)
}

func (pm *ProcessManager) changeState(state ProcessState) {
	// Изменяем состояние
	pm.mutex.Lock()
	pm.state = state
	pm.mutex.Unlock()

	// Уведомляем об изменении состояния
	select {
	case pm.stateChanged <- struct{}{}:
	default:
	}
}
