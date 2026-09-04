#
# Always cross-compiles to arm-linux-gnueabihf: the R36S runs an aarch64
# kernel, but the only native library the game ships is armeabi-v7a, and the
# bionic ELF loader has to map it into a process of the same word size.
#
# Sources are picked up by wildcard on purpose — the loader, the JNI shim and
# the libc thunks all land as new directories in later milestones and must not
# each require a Makefile edit.

ARCH       ?= arm-linux-gnueabihf
CROSS      ?= $(ARCH)-
CXX        := $(CROSS)g++
PKG_CONFIG ?= $(CROSS)pkg-config

TARGET  := build/katamari
OBJDIR  := build/obj

PKGS := sdl2 zlib libzip libmpg123 vorbisfile

OPT  ?= -O2 -g
WARN := -Wall -Wextra -Wno-unused-parameter -Werror=return-type

# thunks/libc/generated holds the bionic export tables produced by
# tools/generate_libc.sh. They are checked in because the build image has no
# clang; see that script for why.
INCLUDES := -I. -Isrc -Iandroid -Iloader -Ithunks -Ithunks/libc \
            -Ithunks/libc/generated -Ijni
CPPFLAGS := $(INCLUDES) $(shell $(PKG_CONFIG) --cflags $(PKGS)) -MMD -MP
# gnu++20: the vendored ELF loader uses std::string::starts_with.
CXXFLAGS := -std=gnu++20 $(OPT) $(WARN) -fno-strict-aliasing -fuse-cxa-atexit
LDFLAGS  := $(OPT)
LDLIBS   := $(shell $(PKG_CONFIG) --libs $(PKGS)) -pthread -lm -ldl -lrt -lbsd

SRCDIRS := src android loader thunks/libc thunks/khronos jni jni/classes \
           third_party/powervr
SRCS    := $(foreach d,$(SRCDIRS),$(wildcard $(d)/*.cpp))
OBJS    := $(patsubst %.cpp,$(OBJDIR)/%.cpp.o,$(SRCS))
DEPS    := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJDIR)/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# The .so files the zip has to carry. Generated, never checked in: they are
# Debian binaries and tools/collect_libs.sh reproduces them exactly.
libs: $(TARGET)
	tools/collect_libs.sh $(TARGET) build/libs.armhf
	tools/check_glibc_floor.sh $(TARGET) build/libs.armhf

clean:
	rm -rf build

.PHONY: all clean libs

-include $(DEPS)


# ---------------------------------------------------------------------------
# Open Citadel
#
# A separate link target intentionally reuses the compatibility substrate
# without linking Katamari's main(), symbol profile, input layer, or audio
# implementation. OC_CXX/OC_ARCH_FLAGS are independent from the ARMHF defaults
# above so the APK's bundled i386 UE3 binary can be exercised on Linux CI.
# ---------------------------------------------------------------------------

OC_CXX        ?= $(CXX)
OC_PKG_CONFIG ?= $(PKG_CONFIG)
OC_ARCH_FLAGS ?=
OC_TAG        ?= armhf
OC_TARGET     := build/open-citadel-$(OC_TAG)
OC_OBJDIR     := build/open-citadel-obj-$(OC_TAG)
OC_PKGS       := sdl2 zlib

OC_SRCDIRS := src android loader thunks/libc thunks/khronos jni jni/classes \
              third_party/powervr open_citadel
OC_SRCS := $(foreach d,$(OC_SRCDIRS),$(wildcard $(d)/*.cpp))
OC_SRCS := $(filter-out \
    src/main.cpp \
    src/symtab.cpp \
    loader/io_util.cpp \
    android/cursor_draw.cpp \
    android/emulator_control.cpp \
    android/fb_probe.cpp \
    android/katamari_input.cpp \
    jni/classes/katamari.cpp, \
    $(OC_SRCS))
OC_OBJS := $(patsubst %.cpp,$(OC_OBJDIR)/%.cpp.o,$(OC_SRCS))
OC_DEPS := $(OC_OBJS:.o=.d)

OC_INCLUDES := -I. -Isrc -Iandroid -Iloader -Ithunks -Ithunks/libc \
               -Ithunks/libc/generated -Ijni -Iopen_citadel
OC_CPPFLAGS := $(OC_INCLUDES) $(shell $(OC_PKG_CONFIG) --cflags $(OC_PKGS)) -MMD -MP
OC_CXXFLAGS := -std=gnu++20 $(OPT) $(WARN) -fno-strict-aliasing -fuse-cxa-atexit \
               $(OC_ARCH_FLAGS)
OC_LDFLAGS  := $(OPT) $(OC_ARCH_FLAGS)
OC_LDLIBS   := $(shell $(OC_PKG_CONFIG) --libs $(OC_PKGS)) -pthread -lm -ldl -lrt -lbsd

open-citadel: $(OC_TARGET)

$(OC_TARGET): $(OC_OBJS)
	@mkdir -p $(@D)
	$(OC_CXX) $(OC_LDFLAGS) -o $@ $^ $(OC_LDLIBS)

$(OC_OBJDIR)/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(OC_CXX) $(OC_CPPFLAGS) $(OC_CXXFLAGS) -c $< -o $@

open-citadel-x86:
	$(MAKE) open-citadel \
	    OC_CXX=g++ OC_PKG_CONFIG=pkg-config OC_ARCH_FLAGS=-m32 OC_TAG=x86

open-citadel-armhf:
	$(MAKE) open-citadel OC_TAG=armhf

.PHONY: open-citadel open-citadel-x86 open-citadel-armhf

-include $(OC_DEPS)
