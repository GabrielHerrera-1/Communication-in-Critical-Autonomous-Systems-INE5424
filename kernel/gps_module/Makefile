# Build do modulo de kernel GPS (Etapa 4 - Sincronizacao Espacial).
#
# Gera gps.ko compilando contra a arvore do kernel linux-6.15.5 (a mesma do
# kernel/Image usado pelo QEMU). O Kbuild compila gps.c com -c e o linka.
#
#   make            -> gera gps.ko
#   make clean      -> remove artefatos
#   make KDIR=/...  -> aponta para outra arvore de kernel

obj-m += gps.o

KDIR ?= $(abspath $(CURDIR)/../../../kernel-build-x86/linux-6.15.5)
PWD  := $(CURDIR)

all:
	@if [ ! -d "$(KDIR)" ]; then \
		printf '\nERRO: arvore do kernel nao encontrada em:\n  %s\n\n' "$(KDIR)"; \
		printf 'Coloque o kernel-build-x86 um nivel acima do repositorio SO2,\n'; \
		printf 'ou informe o caminho: make KDIR=/caminho/para/linux-6.15.5\n\n'; \
		exit 1; \
	fi
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
