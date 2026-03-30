ARCH ?= riscv
CROSS_COMPILE ?= riscv64-linux-gnu-
JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

SRC_DIR := src
TEST_DIR := tests
BUILD_DIR ?= build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/tests
LOG_DIR ?= logs

QEMU ?= qemu-system-riscv64
QEMU_MATCH ?= $(notdir $(QEMU))
QEMU_CPU ?= auto
QEMU_CPU_FILE := $(BUILD_DIR)/.qemu_cpu
QEMU_CPU_CANDIDATES ?= rv64 sifive-u54 max

PYTHON ?= python3
PKILL ?= pkill

ifeq ($(origin CXX), default)
CXX := $(CROSS_COMPILE)g++
endif
ifeq ($(origin CXX), undefined)
CXX := $(CROSS_COMPILE)g++
endif
CXXFLAGS ?= -static -I$(SRC_DIR) -MMD -MP
LDFLAGS ?=

RUN_QEMU_TEST := ./tests/run_qemu_test.sh

SRCS := $(shell find $(SRC_DIR) -name "*.cpp")
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

CORE_TEST_NAMES := basic v3
BENCHMARK_NAMES := rtt

CORE_TEST_BINS := $(addprefix $(BIN_DIR)/,$(CORE_TEST_NAMES))
BENCHMARK_BINS := $(addprefix $(BIN_DIR)/,$(BENCHMARK_NAMES))
TEST_BINS := $(CORE_TEST_BINS) $(BENCHMARK_BINS)

BASIC_BIN := $(BIN_DIR)/basic
MESH_BIN := $(BIN_DIR)/v3
RTT_BIN := $(BIN_DIR)/rtt

DEPS := $(OBJS:.o=.d) $(TEST_BINS:=.d)

.PHONY: all build prepare-runtime select-qemu-cpu stop-qemu clean-logs test test-basic test-mesh-concurrent measure-rtt measure-rtt-10 logs clean
.SECONDARY: $(TEST_BINS) $(OBJS)
.NOTPARALLEL: test test-basic test-mesh-concurrent measure-rtt measure-rtt-10 select-qemu-cpu prepare-runtime

all: test

build:
	@$(MAKE) --no-print-directory -j$(JOBS) $(CORE_TEST_BINS)
	@echo "[build] binarios de teste atualizados em $(BIN_DIR)."

prepare-runtime: stop-qemu
	@mkdir -p "$(LOG_DIR)"
	@rm -f "$(LOG_DIR)/latest"
	@echo "[runtime] logs serao gravados em $(abspath $(LOG_DIR))."

stop-qemu:
	@if command -v "$(PKILL)" >/dev/null 2>&1; then \
		"$(PKILL)" -x "$(QEMU_MATCH)" >/dev/null 2>&1 || true; \
		echo "[runtime] instancias antigas de $(QEMU_MATCH) encerradas."; \
	else \
		echo "[runtime] pkill indisponivel; seguindo sem encerrar QEMU antigo."; \
	fi

clean-logs:
	@rm -rf "$(LOG_DIR)"
	@echo "[clean-logs] logs removidos."

select-qemu-cpu: prepare-runtime $(BASIC_BIN) $(RUN_QEMU_TEST)
	@set -e; \
	mkdir -p "$(LOG_DIR)/cpu-probes"; \
	if [ "$(QEMU_CPU)" != "auto" ]; then \
		echo "$(QEMU_CPU)" > "$(QEMU_CPU_FILE)"; \
		echo "[select-qemu-cpu] usando QEMU_CPU=$(QEMU_CPU)"; \
		exit 0; \
	fi; \
	rm -f "$(QEMU_CPU_FILE)"; \
	for cpu in $(QEMU_CPU_CANDIDATES); do \
		echo "[select-qemu-cpu] testando cpu=$$cpu"; \
		if TIMEOUT_SEC=60 LOGS_DIR="$(abspath $(LOG_DIR))/cpu-probes" QEMU_BIN="$(QEMU)" QEMU_CPU=$$cpu "$(RUN_QEMU_TEST)" "$(BASIC_BIN)" 1 "cpu-probe-$$cpu" "basic test" >"$(LOG_DIR)/cpu-probes/$$cpu.console.log" 2>&1; then \
			echo "$$cpu" > "$(QEMU_CPU_FILE)"; \
			echo "[select-qemu-cpu] cpu selecionada: $$cpu"; \
			exit 0; \
		fi; \
	done; \
	echo "[select-qemu-cpu] nenhuma CPU compativel funcionou; consulte $(LOG_DIR)/cpu-probes" >&2; \
	exit 1

test: build select-qemu-cpu test-basic test-mesh-concurrent
	@echo "[test] suite completa aprovada."

test-basic: select-qemu-cpu $(BASIC_BIN) $(RUN_QEMU_TEST)
	@echo "[test] rodando cenario basic..."
	@LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(BASIC_BIN)" 1 basic "basic test"
	@echo "[test] cenario basic aprovado."

test-mesh-concurrent: select-qemu-cpu $(MESH_BIN) $(RUN_QEMU_TEST)
	@echo "[test] rodando cenario mesh-concurrent..."
	@LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(MESH_BIN)" 5 mesh-concurrent "concluido com 6 envios e 24 recebimentos validados." 6 24
	@echo "[test] cenario mesh-concurrent aprovado."

measure-rtt: select-qemu-cpu $(RTT_BIN) $(RUN_QEMU_TEST) $(TEST_DIR)/summarize_rtt.py
	@set -e; \
	artifacts_file=$$(mktemp /tmp/so2-rtt-artifacts.XXXXXX); \
	KEEP_ARTIFACTS=1 ARTIFACTS_FILE=$$artifacts_file LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(RTT_BIN)" 5 rtt-benchmark "RTT benchmark concluido."; \
	artifacts_root=$$(cat $$artifacts_file); \
	"$(PYTHON)" "$(TEST_DIR)/summarize_rtt.py" "$$artifacts_root/logs/vm1.log"; \
	echo "[measure-rtt] logs preservados em $$artifacts_root/logs"; \
	rm -f $$artifacts_file

measure-rtt-10: select-qemu-cpu $(RTT_BIN) $(RUN_QEMU_TEST) $(TEST_DIR)/summarize_rtt.py $(TEST_DIR)/measure_rtt_batch.py
	@LOGS_DIR="$(abspath $(LOG_DIR))" "$(PYTHON)" "$(TEST_DIR)/measure_rtt_batch.py" "$(RUN_QEMU_TEST)" "$$(cat "$(QEMU_CPU_FILE)")" 10 "$(RTT_BIN)"

logs:
	@if [ ! -d "$(LOG_DIR)/latest" ]; then \
		echo "[logs] nenhum run recente em $(LOG_DIR)/latest" >&2; \
		exit 1; \
	fi
	@if [ ! -d "$(LOG_DIR)/latest/logs" ]; then \
		echo "[logs] artefatos encontrados, mas sem subdiretorio de logs em $(LOG_DIR)/latest" >&2; \
		exit 1; \
	fi
	@echo "[logs] acompanhando $(LOG_DIR)/latest/logs/vm*.log"
	@tail -f "$(LOG_DIR)"/latest/logs/vm*.log

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR)/%: $(TEST_DIR)/%.cpp $(OBJS)
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) $< $(OBJS) $(LDFLAGS) -o $@

clean:
	rm -rf "$(BUILD_DIR)"
	rm -rf "$(LOG_DIR)"
	find "$(SRC_DIR)" -name "*.o" -delete
	find "$(SRC_DIR)" -name "*.d" -delete
	rm -f "$(TEST_DIR)/basic" "$(TEST_DIR)/v3" "$(TEST_DIR)/rtt"
	rm -f "$(TEST_DIR)/v1" "$(TEST_DIR)/v2"
	rm -f "$(TEST_DIR)"/*.d
	rm -f .qemu_cpu

-include $(DEPS)
