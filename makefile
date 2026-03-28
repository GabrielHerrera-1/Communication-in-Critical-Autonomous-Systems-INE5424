# esse makefile compila separadamente cada arquivo da pasta test, depois joga cada um desses
# pro initramfs da pasta e kernel e finaliza gerando o initramfs_tests.cpio 
# o nome de cada executável e o nome do arquivo sem o .cpp no final
# headers e fontes ficam juntos em src/, organizados por camada (core, network, channel, communication, application)
# pra usar e so digitar make no bash e o novo initramfs é gerado, 
# so a test enche com os binários de cada arquivo e eu não sei como fazer pro makefile limpar

SRC_DIR = src
TEST_DIR = tests
WORKDIR = work
OUTPUT_DIR = ..

CXX = riscv64-linux-gnu-g++
CXXFLAGS = -static -I$(SRC_DIR)

INITRAMFS = kernel/initramfs.cpio
NEW_INITRAMFS = $(OUTPUT_DIR)/initramfs_tests.cpio

SRCS := $(shell find $(SRC_DIR) -name "*.cpp")
OBJS := $(SRCS:.cpp=.o)

TEST_SRCS := $(wildcard $(TEST_DIR)/*.cpp)
TEST_BINS := $(TEST_SRCS:.cpp=)

all: $(NEW_INITRAMFS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_DIR)/%: $(TEST_DIR)/%.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(NEW_INITRAMFS): $(TEST_BINS)
	rm -rf $(WORKDIR)
	mkdir $(WORKDIR)

	cd $(WORKDIR) && cpio -id < ../$(INITRAMFS)

	
	for bin in $(TEST_BINS); do \
		cp $$bin $(WORKDIR)/; \
	done

	cd $(WORKDIR) && find . | cpio -o -H newc > $(NEW_INITRAMFS)

	rm -rf $(WORKDIR)

clean:
	find $(SRC_DIR) -name "*.o" -delete
	rm -f $(TEST_BINS)
	rm -f $(OUTPUT_DIR)/*.cpio
	rm -rf $(WORKDIR)
