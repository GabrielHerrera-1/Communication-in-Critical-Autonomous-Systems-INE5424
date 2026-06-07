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
CXXSTD := -std=c++17
CXXFLAGS ?= -static -I$(SRC_DIR) -MMD -MP
LDFLAGS ?=

RUN_QEMU_TEST := ./tests/run_qemu_test.sh

SRCS := $(shell find $(SRC_DIR) -name "*.cpp")
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

CORE_TEST_NAMES := basic v3 sptp_drift sptp_simple quadrant
BENCHMARK_NAMES := rtt stress rtt_intra

# Etapa 4: modulo de kernel GPS (sincronizacao espacial)
GPS_MODULE_DIR := kernel/gps_module
GPS_KO         := $(GPS_MODULE_DIR)/gps.ko

CORE_TEST_BINS := $(addprefix $(BIN_DIR)/,$(CORE_TEST_NAMES))
BENCHMARK_BINS := $(addprefix $(BIN_DIR)/,$(BENCHMARK_NAMES))
TEST_BINS := $(CORE_TEST_BINS) $(BENCHMARK_BINS)

SPTP_DRIFT_BIN   := $(BIN_DIR)/sptp_drift
SPTP_SIMPLE_BIN  := $(BIN_DIR)/sptp_simple
BASIC_BIN        := $(BIN_DIR)/basic
QUADRANT_BIN     := $(BIN_DIR)/quadrant
MESH_BIN := $(BIN_DIR)/v3
RTT_BIN := $(BIN_DIR)/rtt
STRESS_BIN := $(BIN_DIR)/stress
RTT_INTRA_BIN := $(BIN_DIR)/rtt_intra
INTEREST_FANIN_BIN := $(BIN_DIR)/interest_fanin
INTEREST_FANOUT_BIN := $(BIN_DIR)/interest_fanout
INTEREST_TYPES_BIN := $(BIN_DIR)/interest_types
INTEREST_PERIOD_BIN := $(BIN_DIR)/interest_period
INTEREST_LIFECYCLE_BIN := $(BIN_DIR)/interest_lifecycle
INTEREST_PRESENCE_BIN := $(BIN_DIR)/interest_presence
INTEREST_RSU_REPEAT_BIN := $(BIN_DIR)/interest_rsu_repeat
INTEREST_MOBILITY_BIN := $(BIN_DIR)/interest_mobility
INTEREST_SCALE_BIN := $(BIN_DIR)/interest_scale
INTEREST_VALUE_BIN := $(BIN_DIR)/interest_value
INTEREST_SCALE_MEM ?= 128
LIB_NAME := so2
STATIC_LIB := $(LIB_DIR)/lib$(LIB_NAME).a

DEPS := $(OBJS:.o=.d) $(TEST_BINS:=.d)

.PHONY: all build lib prepare-runtime select-qemu-cpu stop-qemu clean-logs test test-basic test-mesh-concurrent test-stress test-sptp-drift test-sptp-simple test-quadrant test-interest test-interest-all test-interest-fanin test-interest-fanout test-interest-types test-interest-period test-interest-lifecycle test-interest-presence test-interest-rsu-repeat test-interest-mobility test-interest-scale test-interest-value gps-module gps-rebuild measure-rtt measure-rtt-intra measure-rtt-10 logs clean _suite-banner
.SECONDARY: $(TEST_BINS) $(OBJS)
.NOTPARALLEL: test test-basic test-mesh-concurrent measure-rtt measure-rtt-intra measure-rtt-10 select-qemu-cpu prepare-runtime

all: test

build:
	@$(MAKE) --no-print-directory -j$(JOBS) lib $(CORE_TEST_BINS)
	@echo "[build] biblioteca estatica em $(STATIC_LIB)."
	@echo "[build] binarios de teste atualizados em $(BIN_DIR)."

lib: $(STATIC_LIB)
	@echo "[lib] biblioteca estatica atualizada em $(STATIC_LIB)."

# Etapa 4: modulo de kernel GPS (gps.ko).
# O gps.ko fica commitado no repo (mesma versao do kernel/Image), entao o
# teste usa o binario pronto -- ninguem precisa do kernel-build-x86 local.
# Para recompilar apos editar gps.c:
#   make gps-rebuild           (kernel-build-x86 um nivel acima do repo)
#   make gps-rebuild KDIR=...  (arvore de kernel em outro caminho)
KDIR ?= $(abspath $(CURDIR)/../kernel-build-x86/linux-6.15.5)

gps-module:
	@test -f "$(GPS_KO)" || { \
		echo "[gps-module] ERRO: $(GPS_KO) ausente (deveria estar commitado)."; \
		echo "  rode 'make gps-rebuild' para compila-lo."; \
		exit 1; \
	}

gps-rebuild:
	@echo "[gps-module] recompilando modulo de kernel GPS"
	@$(MAKE) --no-print-directory -C $(GPS_MODULE_DIR) KDIR=$(KDIR)
	@echo "[gps-module] modulo pronto em $(GPS_KO)."

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

test: _suite-banner measure-rtt measure-rtt-intra test-quadrant
	@printf '\n'
	@printf '═══════════════════════════════════════════════════════════════════\n'
	@printf '  \xe2\x9c\x94  Suite SO2 concluida (4/4 testes aprovados)\n'
	@printf '═══════════════════════════════════════════════════════════════════\n'

_suite-banner:
	@printf '\n'
	@printf '═══════════════════════════════════════════════════════════════════\n'
	@printf '  SO2 -- Suite de Testes (etapa 4: Sincronizacao Espacial)\n'
	@printf '  sptp-simple . quadrant . measure-rtt . measure-rtt-intra\n'
	@printf '═══════════════════════════════════════════════════════════════════\n'

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
	@printf '\n'
	@printf '\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 sptp-simple \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 6 VMs (RSU + 2 senders + 3 receivers) \xe2\x94\x80\xe2\x94\x80\n\n'
	@TIMEOUT_SEC=180 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(SPTP_SIMPLE_BIN)" 6 sptp-simple "cenario validado."
	@printf '\n  Resultados (offset slave-master por receiver, por sender):\n'
	@for f in $(LOG_DIR)/sptp-simple/latest/logs/vm*.log; do \
		grep -aE "RESUMO" "$$f" | sed 's/^/    /' || true; \
	done
	@printf '\n  \xe2\x9c\x94 sptp-simple aprovado\n'

test-quadrant: select-qemu-cpu $(QUADRANT_BIN) $(RUN_QEMU_TEST) gps-module
	@printf '\n'
	@printf '\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 quadrant \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 9 VMs (4 RSUs uma por quadrante + 5 veiculos moveis), sincronizacao espacial \xe2\x94\x80\xe2\x94\x80\n\n'
	@TIMEOUT_SEC=240 WITH_GPS=1 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(QUADRANT_BIN)" 9 quadrant "cenario validado."
	@printf '\n  Resultados (por VM):\n'
	@for f in $(LOG_DIR)/quadrant/latest/logs/vm*.log; do \
		grep -aE "RESUMO" "$$f" | sed 's/^/    /' || true; \
	done
	@printf '\n  \xe2\x9c\x94 quadrant aprovado\n'

# Etapa 5: Interesse/Resposta (publish-subscribe time-triggered). Suite de 10
# cenarios. O broker da RSU fica SEMPRE ativo (default no rsu.cpp), entao nenhum
# teste precisa de so2.rsu_repeat_us.

# T2: fan-in -- 1 consumidor recebe de N produtores
test-interest-fanin: select-qemu-cpu $(INTEREST_FANIN_BIN) $(RUN_QEMU_TEST)
	@echo "[test] interest-fanin: 7 VMs (RSU + 1 consumidor + 5 produtores)..."
	@TIMEOUT_SEC=120 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(INTEREST_FANIN_BIN)" 7 interest-fanin "cenario validado."
	@for f in $(LOG_DIR)/interest-fanin/latest/logs/vm*.log; do grep -aE "RESUMO" "$$f" | sed 's/^/    /' || true; done
	@echo "[test] interest-fanin aprovado."

# T3+T7: fan-out -- N consumidores (2 periodos) <- 1 produtor (1 thread no mais curto)
test-interest-fanout: select-qemu-cpu $(INTEREST_FANOUT_BIN) $(RUN_QEMU_TEST)
	@echo "[test] interest-fanout: 6 VMs (RSU + 1 produtor + 4 consumidores em 2 periodos)..."
	@TIMEOUT_SEC=120 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(INTEREST_FANOUT_BIN)" 6 interest-fanout "cenario validado."
	@for f in $(LOG_DIR)/interest-fanout/latest/logs/vm*.log; do grep -aE "RESUMO" "$$f" | sed 's/^/    /' || true; done
	@echo "[test] interest-fanout aprovado."

# T8: identidade por Unit -- consumidor so recebe o seu tipo (Speed/Lidar/Radar/Counter)
test-interest-types: select-qemu-cpu $(INTEREST_TYPES_BIN) $(RUN_QEMU_TEST)
	@echo "[test] interest-types: 12 VMs (4 tipos, demux por Unit)..."
	@TIMEOUT_SEC=120 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(INTEREST_TYPES_BIN)" 12 interest-types "cenario validado."
	@for f in $(LOG_DIR)/interest-types/latest/logs/vm*.log; do grep -aE "RESUMO" "$$f" | sed 's/^/    /' || true; done
	@echo "[test] interest-types aprovado."

# periodicidade em escala: aderencia ao periodo por produtor (>=20 produtores)
test-interest-period: select-qemu-cpu $(INTEREST_PERIOD_BIN) $(RUN_QEMU_TEST)
	@echo "[test] interest-period: 22 VMs (RSU + consumidor + 20 produtores)..."
	@TIMEOUT_SEC=240 QEMU_MEM=$(INTEREST_SCALE_MEM) LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(INTEREST_PERIOD_BIN)" 22 interest-period "cenario validado."
	@for f in $(LOG_DIR)/interest-period/latest/logs/vm2.log; do grep -aE "RESUMO" "$$f" | sed 's/^/    /' || true; done
	@echo "[test] interest-period aprovado."

# T5+T9: ciclo de vida -- desinteresse explicito + per-address (A sai, B continua)
test-interest-lifecycle: select-qemu-cpu $(INTEREST_LIFECYCLE_BIN) $(RUN_QEMU_TEST)
	@echo "[test] interest-lifecycle: 7 VMs (entrada/saida dinamica + desinteresse)..."
	@TIMEOUT_SEC=150 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(INTEREST_LIFECYCLE_BIN)" 7 interest-lifecycle "cenario validado."
	@for f in $(LOG_DIR)/interest-lifecycle/latest/logs/vm*.log; do grep -aE "RESUMO" "$$f" | sed 's/^/    /' || true; done
	@echo "[test] interest-lifecycle aprovado."

# T5 (presenca): consumidor sai SEM desinteresse; a RSU detecta por presenca (PTP)
test-interest-presence: select-qemu-cpu $(INTEREST_PRESENCE_BIN) $(RUN_QEMU_TEST)
	@echo "[test] interest-presence: 3 VMs (RSU broker + produtor + consumidor que abandona)..."
	@TIMEOUT_SEC=120 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(INTEREST_PRESENCE_BIN)" 3 interest-presence "cenario validado."
	@for f in $(LOG_DIR)/interest-presence/latest/logs/vm*.log; do grep -aE "ABANDONANDO|desinteresse recebido da RSU|Interest_Tracker" "$$f" | sed 's/^/    /' || true; done
	@echo "[test] interest-presence aprovado."

# late joiner: a RSU repete o interesse para um publisher que entra tarde
test-interest-rsu-repeat: select-qemu-cpu $(INTEREST_RSU_REPEAT_BIN) $(RUN_QEMU_TEST)
	@echo "[test] interest-rsu-repeat: 3 VMs (subscriber silencioso + publisher tardio)..."
	@TIMEOUT_SEC=120 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(INTEREST_RSU_REPEAT_BIN)" 3 interest-rsu-repeat "cenario validado."
	@for f in $(LOG_DIR)/interest-rsu-repeat/latest/logs/vm*.log; do grep -aE "RESUMO|repetido" "$$f" | sed 's/^/    /' || true; done
	@echo "[test] interest-rsu-repeat aprovado."

# T1/T6: mobilidade/handover real (WITH_GPS) -- produtor + consumidor moveis
test-interest-mobility: select-qemu-cpu $(INTEREST_MOBILITY_BIN) $(RUN_QEMU_TEST) gps-module
	@echo "[test] interest-mobility: 3 VMs WITH_GPS (RSU + produtor movel + consumidor movel)..."
	@TIMEOUT_SEC=120 WITH_GPS=1 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(INTEREST_MOBILITY_BIN)" 9 interest-mobility "cenario validado."
	@for f in $(LOG_DIR)/interest-mobility/latest/logs/vm*.log; do grep -aE "RESUMO" "$$f" | sed 's/^/    /' || true; done
	@echo "[test] interest-mobility aprovado."

# escala: >=20 veiculos COM MULTIPLOS componentes (3 prod + 2 cons cada)
test-interest-scale: select-qemu-cpu $(INTEREST_SCALE_BIN) $(RUN_QEMU_TEST)
	@echo "[test] interest-scale: 22 VMs (RSU + 21 veiculos x 5 componentes)..."
	@TIMEOUT_SEC=300 QEMU_MEM=$(INTEREST_SCALE_MEM) LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(INTEREST_SCALE_BIN)" 21 interest-scale "cenario validado."
	@for f in $(LOG_DIR)/interest-scale/latest/logs/vm*.log; do grep -aE "RESUMO" "$$f" | sed 's/^/    /' || true; done
	@echo "[test] interest-scale aprovado."

# modo-valor: consumidor le o dado como variavel (value()/operator Value() + fresh)
test-interest-value: select-qemu-cpu $(INTEREST_VALUE_BIN) $(RUN_QEMU_TEST)
	@echo "[test] interest-value: 3 VMs (RSU + produtor + consumidor modo-valor)..."
	@TIMEOUT_SEC=120 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(INTEREST_VALUE_BIN)" 3 interest-value "cenario validado."
	@for f in $(LOG_DIR)/interest-value/latest/logs/vm*.log; do grep -aE "RESUMO|value\.seq" "$$f" | sed 's/^/    /' || true; done
	@echo "[test] interest-value aprovado."

# suite leve (sem escala/period/mobility -- esses sao pesados/WITH_GPS)
test-interest: test-interest-fanin test-interest-fanout test-interest-types test-interest-lifecycle test-interest-presence test-interest-rsu-repeat test-interest-value
	@echo "[test] suite interest (leve) aprovada."

# suite completa: os 10 cenarios
test-interest-all: test-interest-fanin test-interest-fanout test-interest-types test-interest-period test-interest-lifecycle test-interest-presence test-interest-rsu-repeat test-interest-mobility test-interest-scale test-interest-value
	@echo "[test] suite interest COMPLETA (10) aprovada."

test-stress: select-qemu-cpu $(STRESS_BIN) $(RUN_QEMU_TEST)
	@echo "[test] rodando cenario stress (intra + inter VM, 5 VMs)..."
	@TIMEOUT_SEC=900 LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(STRESS_BIN)" 5 stress "cenario validado."
	@echo "[test] cenario stress aprovado."

measure-rtt: select-qemu-cpu $(RTT_BIN) $(RUN_QEMU_TEST) $(TEST_DIR)/summarize_rtt.py
	@printf '\n'
	@printf '\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 measure-rtt \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 5 VMs, latencia inter-VM via raw socket \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n\n'
	@set -e; \
	artifacts_file=$$(mktemp /tmp/so2-rtt-artifacts.XXXXXX); \
	KEEP_ARTIFACTS=1 ARTIFACTS_FILE=$$artifacts_file LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(RTT_BIN)" 5 rtt-benchmark "RTT benchmark concluido."; \
	artifacts_root=$$(cat $$artifacts_file); \
	printf '\n  Resultados:\n'; \
	"$(PYTHON)" "$(TEST_DIR)/summarize_rtt.py" "$$artifacts_root/logs/vm1.log" | sed 's/^/    /'; \
	printf '    (logs em %s)\n' "$$artifacts_root/logs"; \
	rm -f $$artifacts_file
	@printf '\n  \xe2\x9c\x94 measure-rtt aprovado\n'

measure-rtt-intra: select-qemu-cpu $(RTT_INTRA_BIN) $(RUN_QEMU_TEST) $(TEST_DIR)/summarize_rtt.py
	@printf '\n'
	@printf '\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 measure-rtt-intra \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 1 VM, latencia intra-VM via SHM \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n\n'
	@set -e; \
	artifacts_file=$$(mktemp /tmp/so2-rtt-intra-artifacts.XXXXXX); \
	TIMEOUT_SEC=180 KEEP_ARTIFACTS=1 ARTIFACTS_FILE=$$artifacts_file LOGS_DIR="$(abspath $(LOG_DIR))" QEMU_BIN="$(QEMU)" QEMU_CPU=$$(cat "$(QEMU_CPU_FILE)") "$(RUN_QEMU_TEST)" "$(RTT_INTRA_BIN)" 1 rtt-intra-benchmark "[rtt-intra][initiator] RTT intra benchmark concluido."; \
	artifacts_root=$$(cat $$artifacts_file); \
	printf '\n  Resultados:\n'; \
	"$(PYTHON)" "$(TEST_DIR)/summarize_rtt.py" "$$artifacts_root/logs/vm1.log" | sed 's/^/    /'; \
	printf '    (logs em %s)\n' "$$artifacts_root/logs"; \
	rm -f $$artifacts_file
	@printf '\n  \xe2\x9c\x94 measure-rtt-intra aprovado\n'

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
	@printf '  CXX  %s\n' "$<"
	@$(CXX) $(CXXFLAGS) $(CXXSTD) -c $< -o $@

# pega todos os .o gerados de src/ e empacota num único arquivo .a (biblioteca estática)
# o .a é um arquivo que agrupa código compilado pra ser linkado por outros programas
# r=insere/substitui os .o dentro do .a  c=cria o .a se não existir  s=gera índice interno (acelera o linker)
# $@ = o alvo, ou seja, build/lib/libso2.a
# $^ = todos os pré-requisitos, ou seja, todos os .o de src/
$(STATIC_LIB): $(OBJS)
	@mkdir -p "$(dir $@)"
	@printf '  AR   %s\n' "$@"
	@$(AR) rcs $@ $^

# regra genérica: pra cada .cpp em tests/ gera um binário em build/tests/
# o % é um coringa: test_foo.cpp vira build/tests/test_foo
# $< = o .cpp do teste (primeiro pré-requisito)
# -L = diz pro linker onde procurar a lib  -lso2 = linka libso2.a
# $@ = o binário de saída
$(BIN_DIR)/%: $(TEST_DIR)/%.cpp $(STATIC_LIB)
	@mkdir -p "$(dir $@)"
	@printf '  LD   %s\n' "$@"
	@$(CXX) $(CXXFLAGS) $(CXXSTD) $< -L$(LIB_DIR) -l$(LIB_NAME) $(LDFLAGS) -o $@

clean:
	rm -rf "$(BUILD_DIR)"
	rm -rf "$(LOG_DIR)"
	find "$(SRC_DIR)" -name "*.o" -delete
	find "$(SRC_DIR)" -name "*.d" -delete
	rm -f "$(TEST_DIR)/basic" "$(TEST_DIR)/v3" "$(TEST_DIR)/rtt"
	rm -f "$(TEST_DIR)/v1" "$(TEST_DIR)/v2"
	rm -f "$(TEST_DIR)"/*.d
	rm -f .qemu_cpu
	@$(MAKE) --no-print-directory -C $(GPS_MODULE_DIR) clean

-include $(DEPS)
