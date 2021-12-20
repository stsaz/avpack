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
	$(OUT_DIR)/aac.o \
	$(OUT_DIR)/ape.o \
	$(OUT_DIR)/apetag.o \
	$(OUT_DIR)/avi.o \
	$(OUT_DIR)/caf.o \
	$(OUT_DIR)/cue.o \
	$(OUT_DIR)/flac.o \
	$(OUT_DIR)/flacogg.o \
	$(OUT_DIR)/icy.o \
	$(OUT_DIR)/m3u.o \
	$(OUT_DIR)/main.o \
	$(OUT_DIR)/mkv.o \
	$(OUT_DIR)/mp3.o \
	$(OUT_DIR)/mp4.o \
	$(OUT_DIR)/mpc.o \
	$(OUT_DIR)/ogg.o \
	$(OUT_DIR)/pls.o \
	$(OUT_DIR)/vorbistag.o \
	$(OUT_DIR)/wav.o \
	$(OUT_DIR)/wv.o \
	$(OUT_DIR)/compat.o

all: $(TESTER)

clean:
	$(RM) $(TESTER) $(OBJ)

CFLAGS := -I$(AVPACK_DIR) -I$(FFBASE_DIR) \
	-Wall -Wextra -fvisibility=hidden -fno-strict-aliasing
CFLAGS += -DFF_DEBUG -O0 -g
CXXFLAGS := $(CFLAGS)
CFLAGS += -std=gnu99
# CFLAGS += -fsanitize=address
# LDFLAGS += -fsanitize=address

FFBASE_HDR := $(wildcard $(FFBASE_DIR)/ffbase/*.h)

$(OUT_DIR)/aac.o: $(AVPACK_DIR)/test/aac.c \
		$(AVPACK_DIR)/avpack/aac-read.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/ape.o: $(AVPACK_DIR)/test/ape.c \
		$(AVPACK_DIR)/avpack/ape-read.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/apetag.o: $(AVPACK_DIR)/test/apetag.c \
		$(AVPACK_DIR)/avpack/apetag.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/avi.o: $(AVPACK_DIR)/test/avi.c \
		$(AVPACK_DIR)/avpack/avi-fmt.h \
		$(AVPACK_DIR)/avpack/avi-read.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/caf.o: $(AVPACK_DIR)/test/caf.c \
		$(AVPACK_DIR)/avpack/caf-fmt.h \
		$(AVPACK_DIR)/avpack/caf-read.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/compat.o: $(AVPACK_DIR)/test/compat.cpp \
		$(HDR) \
		$(FFBASE_HDR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(OUT_DIR)/cue.o: $(AVPACK_DIR)/test/cue.c \
		$(AVPACK_DIR)/avpack/cue.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/flac.o: $(AVPACK_DIR)/test/flac.c \
		$(AVPACK_DIR)/avpack/flac-fmt.h \
		$(AVPACK_DIR)/avpack/flac-read.h \
		$(AVPACK_DIR)/avpack/flac-write.h \
		$(AVPACK_DIR)/avpack/vorbistag.h \
		$(AVPACK_DIR)/avpack/mmtag.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/flacogg.o: $(AVPACK_DIR)/test/flacogg.c \
		$(AVPACK_DIR)/avpack/flac-fmt.h \
		$(AVPACK_DIR)/avpack/flac-ogg-read.h \
		$(AVPACK_DIR)/avpack/ogg-fmt.h \
		$(AVPACK_DIR)/avpack/ogg-read.h \
		$(AVPACK_DIR)/avpack/vorbistag.h \
		$(AVPACK_DIR)/avpack/mmtag.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/icy.o: $(AVPACK_DIR)/test/icy.c \
		$(AVPACK_DIR)/avpack/icy.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/m3u.o: $(AVPACK_DIR)/test/m3u.c \
		$(AVPACK_DIR)/avpack/m3u.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/main.o: $(AVPACK_DIR)/test/main.c \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/mkv.o: $(AVPACK_DIR)/test/mkv.c \
		$(AVPACK_DIR)/avpack/mkv-fmt.h \
		$(AVPACK_DIR)/avpack/mkv-read.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/mp3.o: $(AVPACK_DIR)/test/mp3.c \
		$(AVPACK_DIR)/avpack/apetag.h \
		$(AVPACK_DIR)/avpack/id3v2.h \
		$(AVPACK_DIR)/avpack/id3v2.h \
		$(AVPACK_DIR)/avpack/mp3-read.h \
		$(AVPACK_DIR)/avpack/mp3-write.h \
		$(AVPACK_DIR)/avpack/mpeg1-fmt.h \
		$(AVPACK_DIR)/avpack/mpeg1-read.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/mp4.o: $(AVPACK_DIR)/test/mp4.c \
		$(AVPACK_DIR)/avpack/mp4-fmt.h \
		$(AVPACK_DIR)/avpack/mp4-read.h \
		$(AVPACK_DIR)/avpack/mp4-write.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/mpc.o: $(AVPACK_DIR)/test/mpc.c \
		$(AVPACK_DIR)/avpack/apetag.h \
		$(AVPACK_DIR)/avpack/mpc-read.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/ogg.o: $(AVPACK_DIR)/test/ogg.c \
		$(AVPACK_DIR)/avpack/ogg-fmt.h \
		$(AVPACK_DIR)/avpack/ogg-read.h \
		$(AVPACK_DIR)/avpack/ogg-write.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/pls.o: $(AVPACK_DIR)/test/pls.c \
		$(AVPACK_DIR)/avpack/pls.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/vorbistag.o: $(AVPACK_DIR)/test/vorbistag.c \
		$(AVPACK_DIR)/avpack/vorbistag.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/wav.o: $(AVPACK_DIR)/test/wav.c \
		$(AVPACK_DIR)/avpack/wav-fmt.h \
		$(AVPACK_DIR)/avpack/wav-read.h \
		$(AVPACK_DIR)/avpack/wav-write.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(OUT_DIR)/wv.o: $(AVPACK_DIR)/test/wv.c \
		$(AVPACK_DIR)/avpack/wv-read.h \
		$(AVPACK_DIR)/avpack/shared.h \
		$(FFBASE_HDR)
	$(C) $(CFLAGS) $< -o $@

$(TESTER): $(OBJ)
	$(LINKER) $(LDFLAGS) $+ -o $@
