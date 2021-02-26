# avpack test makefile
# 2021, Simon Zolin

# set OS
ifndef $(OS)
	uname := $(shell uname)
	ifeq ($(uname),Linux)
		OS := linux
	else ifeq ($(uname),FreeBSD)
		OS := freebsd
	else ifeq ($(uname),Darwin)
		OS := apple
	else
		OS := win
	endif
endif

# set compiler
CPREFIX :=
# CPREFIX := x86_64-w64-mingw32-
COMPILER := gcc
ifeq ($(OS),freebsd)
	COMPILER := clang
endif
ifeq ($(OS),apple)
	COMPILER := clang
endif
C := $(CPREFIX)gcc -c
CXX := $(CPREFIX)g++ -c
LINKER := $(CPREFIX)gcc
ifeq ($(COMPILER),clang)
	C := clang -c
	CXX := clang++ -c
	LINKER := clang
endif

# set utils
RM := rm -f
CP := cp -u
SO := so
ifeq ($(OS),win)
	SO := dll
else ifeq ($(OS),apple)
	SO := dylib
endif

ROOT := ..
AVPACK_DIR := $(ROOT)/avpack
FFBASE_DIR := $(ROOT)/ffbase
HDR := $(wildcard $(AVPACK_DIR)/avpack/*.h)

OUT_DIR := $(AVPACK_DIR)/test
TESTER := $(OUT_DIR)/avpack-test
ifeq ($(OS),win)
	TESTER := $(OUT_DIR)/avpack-test.exe
endif
OBJ := \
	$(OUT_DIR)/avi.o \
	$(OUT_DIR)/caf.o \
	$(OUT_DIR)/main.o \
	$(OUT_DIR)/mkv.o \
	$(OUT_DIR)/mp4.o \
	$(OUT_DIR)/ogg.o \
	$(OUT_DIR)/wav.o \
	$(OUT_DIR)/compat.o

all: $(TESTER)

clean:
	$(RM) $(TESTER) $(OBJ)

CFLAGS := -I$(AVPACK_DIR) -I$(FFBASE_DIR) \
	-Wall -Wextra -Werror -fvisibility=hidden
CFLAGS += -DFF_DEBUG -O0 -g
CXXFLAGS := $(CFLAGS)
CFLAGS += -std=gnu99
# CFLAGS += -fsanitize=address
# LDFLAGS += -fsanitize=address

$(OUT_DIR)/%.o: $(AVPACK_DIR)/test/%.c $(HDR) $(AVPACK_DIR)/test/Makefile
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/%.o: $(AVPACK_DIR)/test/%.cpp $(HDR) $(AVPACK_DIR)/test/Makefile
	$(CXX) $(CXXFLAGS) $< -o $@

$(TESTER): $(OBJ)
	$(LINKER) $(LDFLAGS) $+ -o $@
