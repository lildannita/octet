# Makefile for octet
# =====================================================================

# ————————————————————————————————————— Variables —————————————————————————————————————
BUILD_DIR        ?= build
TEST_DIR         ?= build_test
CMAKE            ?= cmake
CMAKE_BUILD_TYPE ?= Release
INSTALL_PREFIX   ?= /usr/local
CMAKE_GENERATOR  ?= "Ninja"

# Default CMake configure
CMAKE_FLAGS      := \
  	-S . \
  	-G $(CMAKE_GENERATOR) \
  	-DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX)

# CMake configure for build/install
CMAKE_BUILD_FLAGS := \
	$(CMAKE_FLAGS) \
	-B $(BUILD_DIR) \
	-DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

# CMake configure for tests/coverage
CMAKE_TEST_FLAGS := \
	$(CMAKE_FLAGS) \
	-B $(TEST_DIR) \
	-DCMAKE_BUILD_TYPE=Debug \
	-DOCTET_BUILD_TESTS=ON

# Make configure
MAKEFLAGS        += --no-print-directory

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
.PHONY: all build rebuild build-tests build-coverage tests coverage-static coverage-shared \ 
		coverage install uninstall clean distclean testclean

# ————————————————————————————————————— Default —————————————————————————————————————
all: build

# ————————————————————————————————————— Configure & Build —————————————————————————————————————
build:
	@echo "=== Configuring ($(CMAKE_BUILD_TYPE)) & building ==="
	@mkdir -p $(BUILD_DIR)
	$(CMAKE) $(CMAKE_BUILD_FLAGS)
	$(CMAKE) --build $(BUILD_DIR)

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
install: distclean build
	@echo "=== Installing to '$(INSTALL_PREFIX)' ==="
	@$(CMAKE) --build $(BUILD_DIR) --target install

uninstall:
	@echo "=== Uninstalling from '$(INSTALL_PREFIX)' ==="
	$(CMAKE) -P $(BUILD_DIR)/octet-uninstall.cmake || true

# ————————————————————————————————————— Cleaning —————————————————————————————————————
clean:
	@echo "=== Cleaning build outputs ==="
	@$(CMAKE) --build $(BUILD_DIR) --target clean || true

distclean:
	@if [ -d "$(BUILD_DIR)" ]; then \
		echo "=== Removing build directory '$(BUILD_DIR)' ==="; \
		rm -rf $(BUILD_DIR); \
	fi

testclean:
	@if [ -d "$(TEST_DIR)" ]; then \
		echo "=== Removing test directory '$(TEST_DIR)' ==="; \
		rm -rf $(TEST_DIR); \
	fi
