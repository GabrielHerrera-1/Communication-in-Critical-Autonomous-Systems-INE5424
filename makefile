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
# Added -MMD -MP to generate .d dependency files automatically
CXXFLAGS = -static -I$(SRC_DIR) -MMD -MP

INITRAMFS = kernel/initramfs.cpio
NEW_INITRAMFS = $(OUTPUT_DIR)/initramfs_tests.cpio

# Recursively find all .cpp files in src
SRCS := $(shell find $(SRC_DIR) -name "*.cpp")
OBJS := $(SRCS:.cpp=.o)
# Collect all generated dependency files
DEPS := $(SRCS:.cpp=.d) $(wildcard $(TEST_DIR)/*.d)

TEST_SRCS := $(wildcard $(TEST_DIR)/*.cpp)
TEST_BINS := $(TEST_SRCS:$(TEST_DIR)/%.cpp=$(TEST_DIR)/%)

.PHONY: all clean

all: $(NEW_INITRAMFS)

# Compile .cpp to .o and generate .d files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Link test binaries
$(TEST_DIR)/%: $(TEST_DIR)/%.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(NEW_INITRAMFS): $(TEST_BINS)
	rm -rf $(WORKDIR)
	mkdir -p $(WORKDIR)
	# Extract old initramfs
	cd $(WORKDIR) && cpio -id < ../$(INITRAMFS)
	# Copy new test binaries into the workdir
	cp $(TEST_BINS) $(WORKDIR)/
	# Create the new initramfs
	cd $(WORKDIR) && find . | cpio -o -H newc > ../$(NEW_INITRAMFS)
	rm -rf $(WORKDIR)

clean:
	find $(SRC_DIR) -name "*.o" -delete
	find $(SRC_DIR) -name "*.d" -delete
	rm -f $(TEST_BINS)
	rm -f $(TEST_DIR)/*.d
	rm -f $(NEW_INITRAMFS)
	rm -rf $(WORKDIR)

# Include the dependency files so make knows about header changes
-include $(DEPS)
