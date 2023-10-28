# avpack test makefile

ROOT := ../..
AVPACK_DIR := $(ROOT)/avpack
FFBASE_DIR := $(ROOT)/ffbase

include $(FFBASE_DIR)/conf.mk

TESTER := avpack-test
ifeq "$(OS)" "windows"
	TESTER := avpack-test.exe
endif

OBJ := \
	aac.o \
	ape.o \
	apetag.o \
	avi.o \
	bmp.o \
	caf.o \
	cue.o \
	flac.o \
	flacogg.o \
	icy.o \
	jpg.o \
	m3u.o \
	main.o \
	mkv.o \
	mp3.o \
	mp4.o \
	mpc.o \
	ogg.o \
	pls.o \
	png.o \
	vorbistag.o \
	wav.o \
	wv.o \
	compat.o

default: $(TESTER)

clean:
	$(RM) $(TESTER) $(OBJ)

CFLAGS += -MMD -MP -I$(AVPACK_DIR) -I$(FFBASE_DIR) \
	-Wall -Wextra \
	-fvisibility=hidden -fno-strict-aliasing
CFLAGS += -DFF_DEBUG -O0 -g
CXXFLAGS += $(CFLAGS)
CFLAGS += -std=gnu99
# CFLAGS += -fsanitize=address
# LINKFLAGS += -fsanitize=address

-include $(wildcard *.d)

%.o: $(AVPACK_DIR)/test/%.c
	$(C) $(CFLAGS) $< -o $@

%.o: $(AVPACK_DIR)/test/%.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

$(TESTER): $(OBJ)
	$(LINK) $+ $(LINKFLAGS) -o $@
