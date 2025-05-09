# Makefile for octet
# =====================================================================

# ————————————————————————————————————— Variables —————————————————————————————————————
OCTET 		     := octet

BUILD_DIR        ?= build
TEST_DIR         ?= build_test
CMAKE            ?= cmake
CMAKE_BUILD_TYPE ?= Release
INSTALL_PREFIX   ?= /usr/local
CMAKE_GENERATOR  ?= "Ninja"
GO      		 ?= go

# Library type
BUILD_SHARED     ?= ON
BUILD_STATIC     ?= OFF

# Go server configuration
GO_SERVER_SOURCE := app/server
GO_BIN_NAME      := ${OCTET}-server
GO_BIN_PATH      := $(BUILD_DIR)/bin/$(GO_BIN_NAME)
GO_BUILD_CMD     := go build
GO_BUILD_FLAGS   := -v -o ${GO_BIN_NAME}// ${GO_BIN_PATH}
GO_ENV           := CGO_ENABLED=0
GO_SERVER_CONFIG ?= $(GO_SERVER_SOURCE)/config.json

# Binary paths
BUILD_BIN_DIR    := $(BUILD_DIR)/bin
OCTET_BIN        := $(BUILD_BIN_DIR)/${OCTET}
OCTET_SERVER_BIN := $(BUILD_BIN_DIR)/$(GO_BIN_NAME)

# Make configure
MAKEFLAGS        += --no-print-directory

# Default CMake configure
CMAKE_FLAGS := \
  	-S . \
  	-G $(CMAKE_GENERATOR) \
  	-DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX)

# CMake configure for build/install
CMAKE_BUILD_FLAGS := \
	$(CMAKE_FLAGS) \
	-B $(BUILD_DIR) \
	-DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) \
	-DOCTET_BUILD_SHARED_LIB=$(BUILD_SHARED) \
	-DOCTET_BUILD_STATIC_LIB=$(BUILD_STATIC) \
	-DOCTET_BUILD_APP=$(BUILD_APP)

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
.PHONY: all build rebuild build-app build-server build-all build-tests \
		build-coverage tests coverage-static coverage-shared coverage \
		install install-all uninstall clean distclean testclean \
		run-server help dev lint

# ————————————————————————————————————— Help —————————————————————————————————————
help:
	@echo "Octet Makefile Usage:"
	@echo "======================"
	@echo "Main targets:"
	@echo "  all              : Build everything (C++ lib, app and Go server)"
	@echo "  build            : Build C++ library"
	@echo "  build-app        : Build C++ application"
	@echo "  build-server     : Build Go server"
	@echo "  build-all        : Build everything"
	@echo "  run-server       : Run the server"
	@echo "  install          : Install C++ library and application"
	@echo "  install-all      : Install everything including Go server"
	@echo ""
	@echo "Development targets:"
	@echo "  dev              : Quick development build"
	@echo "  tests            : Run all tests"
	@echo "  coverage         : Generate code coverage reports"
	@echo "  lint             : Run linters on code"
	@echo ""
	@echo "Cleaning targets:"
	@echo "  clean            : Clean build outputs"
	@echo "  distclean        : Remove build directory"
	@echo "  testclean        : Remove test directory"
	@echo ""
	@echo "Configuration variables:"
	@echo "  BUILD_DIR        : Build directory (default: $(BUILD_DIR))"
	@echo "  CMAKE_BUILD_TYPE : Build type (default: $(CMAKE_BUILD_TYPE))"
	@echo "  INSTALL_PREFIX   : Installation prefix (default: $(INSTALL_PREFIX))"
	@echo "  SERVER_PORT      : Port for server (default: $(SERVER_PORT))"
	@echo "  SERVER_STORAGE   : Storage path for server (default: $(SERVER_STORAGE))"

# ————————————————————————————————————— Default —————————————————————————————————————
all: build-all

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

build-server: build-app
	@echo "=== Building Go server ==="
	@mkdir -p $(GO_SERVER_SOURCE)/bin
	@cd $(GO_SERVER_SOURCE) && \
	$(GO_ENV) $(GO_BUILD_CMD) $(GO_BUILD_FLAGS) \
		-o bin/$(GO_BIN_NAME) \
		-ldflags "-X main.OctetPath=/$(OCTET_BIN)" \
		./cmd/octet-server
	@cp $(GO_SERVER_SOURCE)/bin/$(GO_BIN_NAME) $(BUILD_DIR)/

build-app: build-cli build-server

build-all: build-app build-server
	@echo "=== Build completed successfully ==="
	@echo "C++ binary: $(OCTET_BIN)"
	@echo "Server binary: $(OCTET_SERVER_BIN)"

rebuild: distclean all

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

# ————————————————————————————————————— Development —————————————————————————————————————
dev: CMAKE_BUILD_TYPE=Debug
dev:
	@echo "=== Quick development build ==="
	@mkdir -p $(BUILD_DIR)
	$(CMAKE) $(CMAKE_BUILD_FLAGS) -DOCTET_BUILD_APP=ON
	$(CMAKE) --build $(BUILD_DIR) -- -j$$(nproc)
	@$(MAKE) build-server

lint:
	@echo "=== Running linters ==="
	@if command -v clang-format >/dev/null; then \
		echo "Running clang-format..."; \
		find src include app tests -name "*.cpp" -o -name "*.hpp" -not -path "./*/3rdparty/*" | xargs clang-format -i; \
	else \
		echo "clang-format not found, skipping C++ linting"; \
	fi
	@if command -v golangci-lint >/dev/null; then \
		echo "Running golangci-lint..."; \
		cd $(GO_SERVER_SOURCE) && golangci-lint run ./...; \
	else \
		echo "golangci-lint not found, skipping Go linting"; \
	fi

# ————————————————————————————————————— Run & Test —————————————————————————————————————
tests: build-tests
	@echo "=== Running CTest ==="
	@ctest --test-dir $(TEST_DIR)/tests --verbose

run-server: build-all
	@echo "=== Running server on port $(SERVER_PORT) ==="
	@echo "Storage path: $(SERVER_STORAGE)"
	@echo "Socket path: $(SERVER_SOCKET)"
	@echo "Configuration: $(SERVER_CONFIG)"
	@mkdir -p $(SERVER_STORAGE)
	@cd $(BUILD_DIR) && ./$(GO_BIN_NAME) \
		--port=$(SERVER_PORT) \
		--storage=$(SERVER_STORAGE) \
		--socket=$(SERVER_SOCKET) \
		--config=$(SERVER_CONFIG)

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
install: build-app
	@echo "=== Installing C++ components to '$(INSTALL_PREFIX)' ==="
	@$(CMAKE) --build $(BUILD_DIR) --target install

install-all: install build-server
	@echo "=== Installing Go server to '$(INSTALL_PREFIX)/bin' ==="
	@mkdir -p $(INSTALL_PREFIX)/bin
	@mkdir -p $(INSTALL_PREFIX)/etc/octet
	@cp $(BUILD_DIR)/$(GO_BIN_NAME) $(INSTALL_PREFIX)/bin/
	@cp $(GO_SERVER_SOURCE)/config.json $(INSTALL_PREFIX)/etc/octet/
	@echo "Installation complete!"
	@echo "  - C++ library installed to $(INSTALL_PREFIX)/lib"
	@echo "  - C++ application installed to $(INSTALL_PREFIX)/bin/octet"
	@echo "  - Go server installed to $(INSTALL_PREFIX)/bin/$(GO_BIN_NAME)"
	@echo "  - Configuration installed to $(INSTALL_PREFIX)/etc/octet/config.json"

uninstall:
	@echo "=== Uninstalling from '$(INSTALL_PREFIX)' ==="
	@$(CMAKE) -P $(BUILD_DIR)/octet-uninstall.cmake || true
	@echo "Removing Go server..."
	@rm -f $(INSTALL_PREFIX)/bin/$(GO_BIN_NAME)
	@rm -f $(INSTALL_PREFIX)/etc/octet/config.json
	@if [ -d "$(INSTALL_PREFIX)/etc/octet" ]; then rmdir --ignore-fail-on-non-empty $(INSTALL_PREFIX)/etc/octet; fi

# ————————————————————————————————————— Cleaning —————————————————————————————————————
clean:
	@echo "=== Cleaning build outputs ==="
	@$(CMAKE) --build $(BUILD_DIR) --target clean || true
	@rm -f $(GO_SERVER_SOURCE)/bin/$(GO_BIN_NAME)
	@rm -f $(BUILD_DIR)/$(GO_BIN_NAME)

distclean:
	@if [ -d "$(BUILD_DIR)" ]; then \
		echo "=== Removing build directory '$(BUILD_DIR)' ==="; \
		rm -rf $(BUILD_DIR); \
	fi
	@if [ -d "$(GO_SERVER_SOURCE)/bin" ]; then \
		echo "=== Removing Go binary directory '$(GO_SERVER_SOURCE)/bin' ==="; \
		rm -rf $(GO_SERVER_SOURCE)/bin; \
	fi

testclean:
	@if [ -d "$(TEST_DIR)" ]; then \
		echo "=== Removing test directory '$(TEST_DIR)' ==="; \
		rm -rf $(TEST_DIR); \
	fi
