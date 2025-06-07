# avpack test makefile

ROOT := ../..
AVPACK_DIR := $(ROOT)/avpack
FFBASE_DIR := $(ROOT)/ffbase

include $(FFBASE_DIR)/conf.mk

TESTER := avpack-test
ifeq "$(OS)" "windows"
	TESTER := avpack-test.exe
endif

OBJ := main.o \
	bmp.o \
	jpg.o \
	png.o \
	\
	m3u.o \
	pls.o \
	\
	apetag.o \
	vorbistag.o \
	\
	cue.o \
	icy.o \
	compat.o \
	reader.o

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
