# project setup
PROJ = AmiBroWser
BUILD_DIR = .build
SRC_DIR = .
VERSION_H = $(BUILD_DIR)/version.h
OUTPUT = $(BUILD_DIR)/$(PROJ)
OUTPUT_ICON = $(OUTPUT).info
OUTPUT_MAP = $(BUILD_DIR)/$(PROJ).map
HOST_OUTPUT = $(shell wslpath -a -w $(OUTPUT))
HOST_ICON = $(shell wslpath -a -w $(OUTPUT_ICON))

# toolchain setup
TOOLCHAIN := m68k-amigaos-
CC := $(TOOLCHAIN)gcc
CXX := $(TOOLCHAIN)g++
STRIP := $(TOOLCHAIN)strip
CMD := cmd.exe

CFLAGS := \
	-m68010 \
	-mtune=68010 \
	-mcrt=nix13 \
	-funsigned-char \
	-Os \
	-fjump-tables \
	-fomit-frame-pointer \
	-foptimize-strlen \
	-ffunction-sections \
	-fdata-sections \
	-I$(BUILD_DIR)

CXXFLAGS := -I$(BUILD_DIR)

CPPFLAGS := \
	-Wall \
	-Wextra \
	-Werror \
	-Wno-error=unused-function \
	-Wno-error=unused-parameter \
	-Wno-error=unused-variable \
	-Wno-missing-field-initializers \
	-Wno-strict-aliasing \
	-Wno-pointer-sign \
	-Wno-ignored-qualifiers \
	-Wno-switch \
	-pipe

LDFLAGS := \
	-mcrt=nix13 \
	-Wl,--gc-sections \
	-Wl,-Map=$(OUTPUT_MAP)

ifeq ($(NLOG), 1)
CPPFLAGS := $(CPPFLAGS) -DNLOG=1
endif

ifeq ($(NDEBUG), 1)
CPPFLAGS := $(CPPFLAGS) -DNDEBUG=1
endif

# List source files
SRCS=$(wildcard $(SRC_DIR)/*.c)
SRCS_S=$(wildcard $(SRC_DIR)/*.s)
SRCS_CPP=$(wildcard $(SRC_DIR)/*.cpp)
SRCS_H=$(wildcard $(SRC_DIR)/*.h)

# List of object files generated from source files
OBJS=$(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS)) $(patsubst $(SRC_DIR)/%.s, $(BUILD_DIR)/%.o, $(SRCS_S)) $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS_CPP))

.PHONY: all generate_version

all: $(OUTPUT)

dump:
	@echo $(OBJS)
	@echo $(SRCS)
	@echo $(SRCS_S)
	@echo $(CC)

generate_version: $(BUILD_DIR)
	@echo -n "#define GIT_VERSION " > $(VERSION_H).tmp
	@git describe --tags --always --dirty 2> /dev/null >> $(VERSION_H).tmp || echo "unknown" >> $(VERSION_H).tmp
	@cmp -s $(VERSION_H) $(VERSION_H).tmp || (mv $(VERSION_H).tmp $(VERSION_H) && echo "Generated '$(VERSION_H)'")
	@rm -f $(VERSION_H).tmp

$(VERSION_H): generate_version

# Generated object files
$(BUILD_DIR):
	@mkdir -pv $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s $(VERSION_H) $(SRCS_H) Makefile
	@echo "Compiling '$<'"
	@$(CC) $(CPPFLAGS) -DBASE_FILE_NAME=$< -x assembler-with-cpp $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(VERSION_H) $(SRCS_H) Makefile
	@echo "Compiling '$<'"
	@$(CC) $(CPPFLAGS) $(CFLAGS) -DBASE_FILE_NAME=$< -c -o $@ $<

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(VERSION_H) $(SRCS_H) Makefile
	@echo "Compiling '$<'"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DBASE_FILE_NAME=$< -c -o $@ $<

$(OUTPUT_ICON):
	cp assets/icon.info $(OUTPUT_ICON)

# Generate executable
$(OUTPUT): $(OBJS) $(OUTPUT_ICON)
	@echo "Linking '$@'"
	@$(CC) $(CPPFLAGS) $(LDFLAGS) $(OBJS) -o $@
ifeq ($(DOSTRIP), 1)
	@echo "Stripping '$@'"
	@$(STRIP) $@
endif
	@echo -n "Output file size: "
	@du -bh "$@" | cut -f1

clean:
	@rm -fv $(OBJS) $(VERSION_H) $(OUTPUT) $(OUTPUT_ICON) $(OUTPUT_MAP)

deploy: $(OUTPUT)
	@echo "Copying stripped '$(OUTPUT)' to floppy..."
	@$(STRIP) $(OUTPUT)
	@$(CMD) /C copy '$(HOST_OUTPUT)' '$(HOST_ICON)' C:\\
