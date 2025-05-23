# Makefile for octet
# =====================================================================

# ————————————————————————————————————— Variables —————————————————————————————————————
OCTET 		     := octet

# Library type
BUILD_SHARED     ?= ON
BUILD_STATIC     ?= OFF

# Install config
INSTALL_PREFIX   ?= /usr/local

# Path for packaging Docker image
DOCKER_ARCHIVE	 ?= docker/octet-image.tar

BUILD_DIR        := $(abspath build)
TEST_DIR         := $(abspath build_test)
CMAKE            := cmake
CMAKE_BUILD_TYPE := Release
CMAKE_GENERATOR  := "Ninja"
GO      		 := go

# Go server configuration
GO_SERVER_SOURCE := $(abspath app/server)
GO_BIN_NAME      := $(OCTET)-server
GO_TARGET_PATH 	 := $(GO_SERVER_SOURCE)/cmd/$(GO_BIN_NAME)
GO_BIN_PATH      := $(BUILD_DIR)/bin/$(GO_BIN_NAME)
GO_BUILD_FLAGS   := -v -o $(GO_BIN_PATH)
GO_ENV           := CGO_ENABLED=0

# As `install` targets runs from sudo, trying to get correct path to `go` directory
REAL_USER := $(shell if [ -n "$$SUDO_USER" ]; then echo $$SUDO_USER; else echo $$USER; fi)
USER_HOME := $(shell if [ -n "$$SUDO_USER" ]; then getent passwd $$SUDO_USER | cut -d: -f6; else echo $$HOME; fi)
GO_INSTALL_PATH := $(USER_HOME)/go/bin

# Binary paths
BUILD_BIN_DIR    := $(BUILD_DIR)/bin
OCTET_BIN        := $(BUILD_BIN_DIR)/$(OCTET)
OCTET_SERVER_BIN := $(BUILD_BIN_DIR)/$(GO_BIN_NAME)

# Make configure
MAKEFLAGS        += --no-print-directory

# Default CMake configure
CMAKE_FLAGS := \
  	-S . \
  	-G $(CMAKE_GENERATOR) \
  	-DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX)

# CMake configure for build/install
CMAKE_COMMON_BUILD_FLAGS := \
	$(CMAKE_FLAGS) \
	-B $(BUILD_DIR) \
	-DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

CMAKE_BUILD_FLAGS := \
	$(CMAKE_COMMON_BUILD_FLAGS) \
	-DOCTET_BUILD_SHARED_LIB=$(BUILD_SHARED) \
	-DOCTET_BUILD_STATIC_LIB=$(BUILD_STATIC)

# CMake configure for tests/coverage
CMAKE_TEST_FLAGS := \
	$(CMAKE_FLAGS) \
	-B $(TEST_DIR) \
	-DCMAKE_BUILD_TYPE=Debug \
	-DOCTET_BUILD_TESTS=ON

# For absolute path output (of coverage reports)
PYTHON := $(shell command -v python3 2>/dev/null || command -v python || echo "")
define print_report_path
	@f="$(1)"; \
	if [ -f "$$f" ]; then \
		if [ -n "$(PYTHON)" ]; then \
			$(PYTHON) -c "import os,sys; print(os.path.abspath(sys.argv[1]))" "$$f"; \
		else \
			echo "Report found: $$f (but cannot resolve full path — Python not available)"; \
		fi \
	else \
		echo "Report not found: $$f"; \
	fi
endef
STATIC_COVERAGE_PATH := $(TEST_DIR)/tests/coverage/octet_coverage_static_report/index.html
SHARED_COVERAGE_PATH := $(TEST_DIR)/tests/coverage/octet_coverage_shared_report/index.html

# ————————————————————————————————————— Phony targets —————————————————————————————————————
.PHONY: all build rebuild rebuild-app build-cli build-app build-tests \
        build-coverage tests coverage-static coverage-shared coverage \
		docker-build docker-image docker-archive docker-run docker-stop \
        install uninstall install-app uninstall-app clean testclean lint \
		openapi help

# ————————————————————————————————————— Help —————————————————————————————————————
help:
	@echo "Octet Makefile Usage:"
	@echo "======================"
	@echo "Main targets:"
	@echo "  all              : Build C++ application and Go server"
	@echo "  build            : Build C++ library only"
	@echo "  build-cli        : Build C++ CLI application"
	@echo "  build-app        : Build C++ CLI and Go server"
	@echo "  rebuild          : Clean and rebuild (library only)"
	@echo "  rebuild-app      : Clean and rebuild application (CLI and Go server)"
	@echo "  install          : Install C++ library to system (may require sudo)"
	@echo "  install-app      : Install C++ library, CLI application and Go server to system (may require sudo)"
	@echo "  uninstall        : Uninstall C++ components from system (may require sudo)"
	@echo "  uninstall-app    : Uninstall all components (C++ and Go) from system (may require sudo)"
	@echo ""
	@echo "Docker targets (for server usage):"
	@echo "  docker-build     : Build C++ CLI and Go server for Docker (with static linking)"
	@echo "  docker-image 	  :	Build Docker image"
	@echo "  docker-archive   :	Arhive Docker image using DOCKER_ARCHIVE path"
	@echo "  docker-run 	  :	Run Docker container (with docker/docker-compose.yaml configuration)"
	@echo "  docker-stop 	  :	Stop Docker container"
	@echo ""
	@echo "Development targets:"
	@echo "  tests            : Run all tests"
	@echo "  coverage         : Generate code coverage reports"
	@echo "  lint             : Run linters on code"
	@echo "  openapi          : Update OpenAPI documentation"
	@echo ""
	@echo "Cleaning targets:"
	@echo "  clean            : Remove build directory and Go binaries"
	@echo "  testclean        : Remove test directory"
	@echo ""
	@echo "Configuration variables:"
	@echo "  BUILD_SHARED     : Build shared library (default: $(BUILD_SHARED))"
	@echo "  BUILD_STATIC     : Build static library (default: $(BUILD_STATIC))"
	@echo "  INSTALL_PREFIX   : Installation prefix (default: $(INSTALL_PREFIX))"
	@echo "  DOCKER_ARCHIVE   : Path for packaging Docker image (default: $(DOCKER_ARCHIVE))"

# ————————————————————————————————————— Default —————————————————————————————————————
all: build-app

# ————————————————————————————————————— Configure & Build —————————————————————————————————————
build:
	@echo "=== Configuring ($(CMAKE_BUILD_TYPE)) & building C++ library ==="
	@mkdir -p $(BUILD_DIR)
	$(CMAKE) $(CMAKE_BUILD_FLAGS) -DOCTET_BUILD_APP=OFF
	$(CMAKE) --build $(BUILD_DIR)

build-cli:
	@echo "=== Configuring ($(CMAKE_BUILD_TYPE)) & building CLI application ==="
	@mkdir -p $(BUILD_DIR)
	$(CMAKE) $(CMAKE_BUILD_FLAGS) -DOCTET_BUILD_APP=ON
	$(CMAKE) --build $(BUILD_DIR)

build-server:
	@echo "=== Building Go server ==="
	@cd $(GO_SERVER_SOURCE) && \
	$(GO_ENV) $(GO) build $(GO_BUILD_FLAGS) \
		-ldflags "-X 'github.com/lildannita/octet-server/internal/config.OctetPath=$(OCTET_BIN)'" \
		$(GO_TARGET_PATH)

build-app: build-cli build-server

rebuild: clean build

rebuild-app: clean build-app

build-tests: testclean
	@echo "=== Building with tests enabled ==="
	@mkdir -p $(TEST_DIR)
	$(CMAKE) $(CMAKE_TEST_FLAGS)
	$(CMAKE) --build $(TEST_DIR)

build-coverage: testclean
	@echo "=== Configuring & building with coverage ==="
	@mkdir -p $(TEST_DIR)
	$(CMAKE) $(CMAKE_TEST_FLAGS) -DOCTET_COVERAGE=ON
	$(CMAKE) --build $(TEST_DIR)

# ————————————————————————————————————— Docker —————————————————————————————————————
docker-build:
	@echo "=== Configuring ($(CMAKE_BUILD_TYPE)) & building CLI application for Docker ==="
	@mkdir -p $(BUILD_DIR)
	$(CMAKE) $(CMAKE_COMMON_BUILD_FLAGS) -DOCTET_BUILD_APP=ON -DOCTET_USE_STATIC_FOR_APP=ON
	$(CMAKE) --build $(BUILD_DIR)
	$(MAKE) build-server

docker-image:
	@echo "=== Building Docker image (octet-image) ==="
	@docker build -f docker/Dockerfile -t octet-image .

docker-archive:
	@echo "=== Archiving Docker image (octet-image) ==="
	@docker save -o $(DOCKER_ARCHIVE) octet-image:latest
	@echo "Path to archive with octet-image: $(DOCKER_ARCHIVE)"

docker-run:
	@echo "=== Running Docker containter ==="
	@cd docker && docker compose up -d

docker-stop:
	@echo "=== Running Docker containter ==="
	@cd docker && docker compose down

# ————————————————————————————————————— Development —————————————————————————————————————
lint:
	@echo "=== Running linters ==="
	@if command -v clang-format >/dev/null; then \
		echo "Running clang-format..."; \
		find . \
			\( -name "3rdparty" -o -name "build*" -o -name ".git" -o -name "node_modules" \) -type d -prune -o \
			\( -name "*.cpp" -o -name "*.hpp" \) -type f -print0 | \
			xargs -0 clang-format -i; \
	else \
		echo "clang-format not found, skipping C++ linting"; \
	fi
	@if command -v go fmt >/dev/null; then \
		echo "Running go fmt..."; \
		cd $(GO_SERVER_SOURCE) && go fmt ./...; \
	else \
		echo "go fmt not found, skipping Go linting"; \
	fi

openapi:
	@cd $(GO_SERVER_SOURCE) && $(GO_INSTALL_PATH)/swag init -g cmd/octet-server/main.go

# ————————————————————————————————————— Run & Test —————————————————————————————————————
tests: build-tests
	@echo "=== Running CTest ==="
	@ctest --test-dir $(TEST_DIR)/tests --verbose

# ————————————————————————————————————— Coverage —————————————————————————————————————
coverage-static: build-coverage
	@echo "=== Generating coverage report (static) ==="
	@$(CMAKE) --build $(TEST_DIR) --target octet_coverage_static
	@echo "Report generated:"
	$(call print_report_path,$(STATIC_COVERAGE_PATH))

coverage-shared: build-coverage
	@echo "=== Generating coverage report (shared) ==="
	@$(CMAKE) --build $(TEST_DIR) --target octet_coverage_shared
	@echo "Report generated:"
	$(call print_report_path,$(SHARED_COVERAGE_PATH))

coverage: build-coverage
	@echo "=== Generating coverage reports ==="
	@$(CMAKE) --build $(TEST_DIR) --target octet_coverage
	@echo "Reports generated:"
	@echo "  Static:"
	$(call print_report_path,$(STATIC_COVERAGE_PATH))
	@echo "  Shared:"
	$(call print_report_path,$(SHARED_COVERAGE_PATH))

# ————————————————————————————————————— Install & Uninstall —————————————————————————————————————
install: build
	@echo "=== Installing octet library to '$(INSTALL_PREFIX)' ==="
	@$(CMAKE) --build $(BUILD_DIR) --target install

install-app: build-app
	@echo "=== Installing octet application to '$(INSTALL_PREFIX)' ==="
	@$(CMAKE) --build $(BUILD_DIR) --target install
	@echo "=== Installing Go server to '$(GO_INSTALL_PATH)' ==="
	@cd $(GO_SERVER_SOURCE) && \
	$(GO_ENV) GOBIN=$(GO_INSTALL_PATH) $(GO) install -v \
		-ldflags "-X 'github.com/lildannita/octet-server/internal/config.OctetPath=$(INSTALL_PREFIX)/bin/$(OCTET)'" \
		$(GO_TARGET_PATH)

uninstall:
	@echo "=== Uninstalling from '$(INSTALL_PREFIX)' ==="
	@$(CMAKE) -P $(BUILD_DIR)/octet-uninstall.cmake || true

uninstall-app: uninstall
	@echo "=== Removing Go server ==="
	@rm -f $(GO_INSTALL_PATH)/$(GO_BIN_NAME)

# ————————————————————————————————————— Cleaning —————————————————————————————————————
clean:
	@if [ -d "$(BUILD_DIR)" ]; then \
		echo "=== Removing build directory '$(BUILD_DIR)' ==="; \
		rm -rf $(BUILD_DIR); \
	fi

testclean:
	@if [ -d "$(TEST_DIR)" ]; then \
		echo "=== Removing test directory '$(TEST_DIR)' ==="; \
		rm -rf $(TEST_DIR); \
	fi