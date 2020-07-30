# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../../Rack-SDK

# CC:= clang
# CXX:= clang++

# FLAGS will be passed to both the C and C++ compiler
FLAGS += -Idep
FLAGS += -Idep/rubberband
FLAGS += -Idep/rubberband/src
CFLAGS +=
CXXFLAGS += -DUSE_KISSFFT
CXXFLAGS += -DUSE_SPEEX

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine, but they should be added to this plugin's build system.
LDFLAGS +=

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp)

# SOURCES += $(wildcard dep/rubberband/src/*.cpp)
# SOURCES += $(wildcard dep/rubberband/src/audiocurves/*.cpp)
# SOURCES += $(wildcard dep/rubberband/src/base/*.cpp)
# SOURCES += $(wildcard dep/rubberband/src/dsp/*.cpp)
# SOURCES += $(wildcard dep/rubberband/src/kissfft/*.c)
# SOURCES += $(wildcard dep/rubberband/src/speex/*.c)
# SOURCES += $(wildcard dep/rubberband/src/system/*.cpp)
# SOURCES += $(wildcard dep/claudio/*.cpp)
# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk

