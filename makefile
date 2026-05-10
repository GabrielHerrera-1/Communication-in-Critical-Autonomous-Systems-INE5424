ARCH ?= x86
CROSS_COMPILE ?=
JOBS ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

SRC_DIR := src
TEST_DIR := tests
BUILD_DIR ?= build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/tests
LIB_DIR := $(BUILD_DIR)/lib
LOG_DIR ?= logs

QEMU ?= qemu-system-x86_64
QEMU_MATCH ?= $(notdir $(QEMU))
QEMU_CPU ?= auto
QEMU_CPU_FILE := $(BUILD_DIR)/.qemu_cpu
QEMU_CPU_CANDIDATES ?= default max qemu64

PYTHON ?= python3
PKILL ?= pkill

ifeq ($(origin CXX), default)
CXX := $(CROSS_COMPILE)g++
endif
ifeq ($(origin CXX), undefined)
CXX := $(CROSS_COMPILE)g++
endif
ifeq ($(origin AR), default)
AR := $(CROSS_COMPILE)ar
endif
ifeq ($(origin AR), undefined)
AR := $(CROSS_COMPILE)ar
endif
CXXFLAGS ?= -static -I$(SRC_DIR) -MMD -MP
LDFLAGS ?=

RUN_QEMU_TEST := ./tests/run_qemu_test.sh

SRCS := $(shell find $(SRC_DIR) -name "*.cpp")
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

CORE_TEST_NAMES := basic v3 sptp_drift sptp_simple
BENCHMARK_NAMES := rtt stress rtt_intra

CORE_TEST_BINS := $(addprefix $(BIN_DIR)/,$(CORE_TEST_NAMES))
BENCHMARK_BINS := $(addprefix $(BIN_DIR)/,$(BENCHMARK_NAMES))
TEST_BINS := $(CORE_TEST_BINS) $(BENCHMARK_BINS)

SPTP_DRIFT_BIN   := $(BIN_DIR)/sptp_drift
SPTP_SIMPLE_BIN  := $(BIN_DIR)/sptp_simple
BASIC_BIN        := $(BIN_DIR)/basic
MESH_BIN := $(BIN_DIR)/v3
RTT_BIN := $(BIN_DIR)/rtt
STRESS_BIN := $(BIN_DIR)/stress
RTT_INTRA_BIN := $(BIN_DIR)/rtt_intra
LIB_NAME := so2
STATIC_LIB := $(LIB_DIR)/lib$(LIB_NAME).a

DEPS := $(OBJS:.o=.d) $(TEST_BINS:=.d)

.PHONY: all build lib prepare-runtime select-qemu-cpu stop-qemu clean-logs test test-basic test-mesh-concurrent test-stress test-sptp-drift test-sptp-simple measure-rtt measure-rtt-intra measure-rtt-10 logs clean
.SECONDARY: $(TEST_BINS) $(OBJS)
.NOTPARALLEL: test test-basic test-mesh-concurrent measure-rtt measure-rtt-10 select-qemu-cpu prepare-runtime

all: test

build:
	@$(MAKE) --no-print-directory -j$(JOBS) lib $(CORE_TEST_BINS)
	@echo "[build] biblioteca estatica em $(STATIC_LIB)."
	@echo "[build] binarios de teste atualizados em $(BIN_DIR)."

lib: $(STATIC_LIB)
	@echo "[lib] biblioteca estatica atualizada em $(STATIC_LIB)."

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

test: test-sptp-simple test-sptp-drift
	@echo
	@echo "[test] suite concluida (sptp-simple + sptp-drift)."

test-basic: select-qemu-cpu $(BASIC_BIN) $(RUN_QEMU_TEST)
	@echo "[test] rodando cenario basic..."
	@LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(BASIC_BIN)" 1 basic "basic test"
	@echo "[test] cenario basic aprovado."

test-mesh-concurrent: select-qemu-cpu $(MESH_BIN) $(RUN_QEMU_TEST)
	@echo "[test] rodando cenario mesh-concurrent..."
	@LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(MESH_BIN)" 5 mesh-concurrent "concluido com 6 envios e 24 recebimentos validados." 6 24
	@echo "[test] cenario mesh-concurrent aprovado."

test-sptp-drift: select-qemu-cpu $(SPTP_DRIFT_BIN) $(RUN_QEMU_TEST)
	@echo "[test] rodando cenario sptp-drift (3 VMs: RSU + master + slave, ~70s)..."
	@TIMEOUT_SEC=180 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(SPTP_DRIFT_BIN)" 3 sptp-drift "cenario validado."
	@echo
	@echo "===== logs sptp-drift ====="
	@for f in $(LOG_DIR)/sptp-drift/latest/logs/vm*.log; do \
		printf '\n--- %s ---\n' "$$f"; \
		grep -aE '^\[[A-Za-z]' "$$f" || true; \
	done
	@echo
	@echo "[test] cenario sptp-drift aprovado."

test-sptp-simple: select-qemu-cpu $(SPTP_SIMPLE_BIN) $(RUN_QEMU_TEST)
	@echo "[test] rodando cenario sptp-simple (5 VMs: RSU + sender + 3 receivers)..."
	@TIMEOUT_SEC=180 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(SPTP_SIMPLE_BIN)" 5 sptp-simple "cenario validado."
	@echo
	@echo "===== logs sptp-simple ====="
	@for f in $(LOG_DIR)/sptp-simple/latest/logs/vm*.log; do \
		printf '\n--- %s ---\n' "$$f"; \
		grep -aE '^\[[A-Za-z]' "$$f" || true; \
	done
	@echo
	@echo "[test] cenario sptp-simple aprovado."

test-stress: select-qemu-cpu $(STRESS_BIN) $(RUN_QEMU_TEST)
	@echo "[test] rodando cenario stress (intra + inter VM, 5 VMs)..."
	@TIMEOUT_SEC=900 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(STRESS_BIN)" 5 stress "cenario validado."
	@echo "[test] cenario stress aprovado."

measure-rtt: select-qemu-cpu $(RTT_BIN) $(RUN_QEMU_TEST) $(TEST_DIR)/summarize_rtt.py
	@set -e; \
	artifacts_file=$$(mktemp /tmp/so2-rtt-artifacts.XXXXXX); \
	KEEP_ARTIFACTS=1 ARTIFACTS_FILE=$$artifacts_file LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(RTT_BIN)" 5 rtt-benchmark "RTT benchmark concluido."; \
	artifacts_root=$$(cat $$artifacts_file); \
	"$(PYTHON)" "$(TEST_DIR)/summarize_rtt.py" "$$artifacts_root/logs/vm1.log"; \
	echo "[measure-rtt] logs preservados em $$artifacts_root/logs"; \
	rm -f $$artifacts_file

measure-rtt-intra: select-qemu-cpu $(RTT_INTRA_BIN) $(RUN_QEMU_TEST) $(TEST_DIR)/summarize_rtt.py
	@set -e; \
	artifacts_file=$$(mktemp /tmp/so2-rtt-intra-artifacts.XXXXXX); \
	TIMEOUT_SEC=180 KEEP_ARTIFACTS=1 ARTIFACTS_FILE=$$artifacts_file LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(RTT_INTRA_BIN)" 1 rtt-intra-benchmark "[rtt-intra][initiator] RTT intra benchmark concluido."; \
	artifacts_root=$$(cat $$artifacts_file); \
	"$(PYTHON)" "$(TEST_DIR)/summarize_rtt.py" "$$artifacts_root/logs/vm1.log"; \
	echo "[measure-rtt-intra] logs preservados em $$artifacts_root/logs"; \
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

# pega todos os .o gerados de src/ e empacota num único arquivo .a (biblioteca estática)
# o .a é um arquivo que agrupa código compilado pra ser linkado por outros programas
# r=insere/substitui os .o dentro do .a  c=cria o .a se não existir  s=gera índice interno (acelera o linker)
# $@ = o alvo, ou seja, build/lib/libso2.a
# $^ = todos os pré-requisitos, ou seja, todos os .o de src/
$(STATIC_LIB): $(OBJS)
	@mkdir -p "$(dir $@)"
	$(AR) rcs $@ $^

# regra genérica: pra cada .cpp em tests/ gera um binário em build/tests/
# o % é um coringa: test_foo.cpp vira build/tests/test_foo
# $< = o .cpp do teste (primeiro pré-requisito)
# -L = diz pro linker onde procurar a lib  -lso2 = linka libso2.a
# $@ = o binário de saída
$(BIN_DIR)/%: $(TEST_DIR)/%.cpp $(STATIC_LIB)
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) $< -L$(LIB_DIR) -l$(LIB_NAME) $(LDFLAGS) -o $@

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
