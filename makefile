SRC_DIR = src
TEST_DIR = tests
QEMU_CPU ?= auto
QEMU_CPU_FILE = .qemu_cpu
QEMU_CPU_CANDIDATES = max rv64 sifive-u54

CXX = riscv64-linux-gnu-g++
CXXFLAGS = -static -I$(SRC_DIR) -MMD -MP

RUN_QEMU_TEST = ./tests/run_qemu_test.sh

SRCS := $(shell find $(SRC_DIR) -name "*.cpp")
OBJS := $(SRCS:.cpp=.o)
CORE_TEST_BINS := $(TEST_DIR)/basic $(TEST_DIR)/v3
BENCHMARK_BINS := $(TEST_DIR)/rtt
TEST_BINS := $(CORE_TEST_BINS) $(BENCHMARK_BINS)

DEPS := $(SRCS:.cpp=.d) $(CORE_TEST_BINS:=.d)

.PHONY: all build select-qemu-cpu test test-basic test-mesh-concurrent measure-rtt measure-rtt-10 clean

all: test

build: $(CORE_TEST_BINS)
	@echo "Cleaning up dependency files..."
	@rm -f $(DEPS)

select-qemu-cpu: $(TEST_DIR)/basic $(RUN_QEMU_TEST)
	@set -e; \
	if [ "$(QEMU_CPU)" != "auto" ]; then \
		echo "$(QEMU_CPU)" > $(QEMU_CPU_FILE); \
		echo "[select-qemu-cpu] usando QEMU_CPU=$(QEMU_CPU)"; \
		exit 0; \
	fi; \
	rm -f $(QEMU_CPU_FILE); \
	for cpu in $(QEMU_CPU_CANDIDATES); do \
		echo "[select-qemu-cpu] testando cpu=$$cpu"; \
		if TIMEOUT_SEC=60 QEMU_CPU=$$cpu $(RUN_QEMU_TEST) tests/basic 1 "cpu-probe-$$cpu" "basic test" >/tmp/so2-qemu-probe-$$cpu.log 2>&1; then \
			echo "$$cpu" > $(QEMU_CPU_FILE); \
			echo "[select-qemu-cpu] cpu selecionada: $$cpu"; \
			exit 0; \
		fi; \
	done; \
	echo "[select-qemu-cpu] nenhuma CPU compativel funcionou; tente QEMU_CPU=rv64 make" >&2; \
	exit 1

test: build select-qemu-cpu test-basic test-mesh-concurrent

test-basic: select-qemu-cpu $(TEST_DIR)/basic $(RUN_QEMU_TEST)
	QEMU_CPU=$$(cat $(QEMU_CPU_FILE)) $(RUN_QEMU_TEST) tests/basic 1 basic "basic test"

test-mesh-concurrent: select-qemu-cpu $(TEST_DIR)/v3 $(RUN_QEMU_TEST)
	QEMU_CPU=$$(cat $(QEMU_CPU_FILE)) $(RUN_QEMU_TEST) tests/v3 5 mesh-concurrent "concluido com 6 envios e 24 recebimentos validados." 6 24

measure-rtt: select-qemu-cpu $(TEST_DIR)/rtt $(RUN_QEMU_TEST) $(TEST_DIR)/summarize_rtt.py
	@set -e; \
	artifacts_file=$$(mktemp /tmp/so2-rtt-artifacts.XXXXXX); \
	KEEP_ARTIFACTS=1 ARTIFACTS_FILE=$$artifacts_file QEMU_CPU=$$(cat $(QEMU_CPU_FILE)) $(RUN_QEMU_TEST) tests/rtt 5 rtt-benchmark "RTT benchmark concluido."; \
	artifacts_root=$$(cat $$artifacts_file); \
	python3 $(TEST_DIR)/summarize_rtt.py $$artifacts_root/logs/vm1.log; \
	echo "[measure-rtt] logs preservados em $$artifacts_root/logs"; \
	rm -f $$artifacts_file

measure-rtt-10: select-qemu-cpu $(TEST_DIR)/rtt $(RUN_QEMU_TEST) $(TEST_DIR)/summarize_rtt.py $(TEST_DIR)/measure_rtt_batch.py
	python3 $(TEST_DIR)/measure_rtt_batch.py $(RUN_QEMU_TEST) $$(cat $(QEMU_CPU_FILE)) 10

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_DIR)/%: $(TEST_DIR)/%.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) $< $(OBJS) -o $@

clean:
	find $(SRC_DIR) -name "*.o" -delete
	find $(SRC_DIR) -name "*.d" -delete
	rm -f $(TEST_BINS)
	rm -f $(TEST_DIR)/v1 $(TEST_DIR)/v2
	rm -f $(TEST_DIR)/*.d
	rm -f $(QEMU_CPU_FILE)

-include $(DEPS)
