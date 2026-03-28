SRC_DIR = src
TEST_DIR = tests
WORKDIR = work
OUTPUT_DIR = .

CXX = riscv64-linux-gnu-g++
# -MMD generates dependency files (.d)
CXXFLAGS = -static -I$(SRC_DIR) -MMD -MP

INITRAMFS = kernel/initramfs.cpio
NEW_INITRAMFS = $(OUTPUT_DIR)/initramfs_tests.cpio

SRCS := $(shell find $(SRC_DIR) -name "*.cpp")
OBJS := $(SRCS:.cpp=.o)
TEST_SRCS := $(wildcard $(TEST_DIR)/*.cpp)
TEST_BINS := $(TEST_SRCS:.cpp=)

# List of all potential dependency files
DEPS := $(SRCS:.cpp=.d) $(TEST_SRCS:.cpp=.d)

.PHONY: all clean

all: $(NEW_INITRAMFS)
	@echo "Cleaning up dependency files..."
	@rm -f $(DEPS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_DIR)/%: $(TEST_DIR)/%.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(NEW_INITRAMFS): $(TEST_BINS)
	rm -rf $(WORKDIR)
	mkdir -p $(WORKDIR)
	cd $(WORKDIR) && cpio -id < ../$(INITRAMFS)
	cp $(TEST_BINS) $(WORKDIR)/
	cd $(WORKDIR) && find . | cpio -o -H newc > ../$(NEW_INITRAMFS)
	rm -rf $(WORKDIR)

clean:
	find $(SRC_DIR) -name "*.o" -delete
	find $(SRC_DIR) -name "*.d" -delete
	rm -f $(TEST_BINS)
	rm -f $(TEST_DIR)/*.d
	rm -f $(NEW_INITRAMFS)
	rm -rf $(WORKDIR)

# Include deps so make sees header changes (only works if files exist)
-include $(DEPS)