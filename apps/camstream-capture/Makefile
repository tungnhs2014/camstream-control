CXX ?= g++
CPPFLAGS ?=
CXXFLAGS ?= -O2
LDFLAGS ?=
LDLIBS ?=

CAMSTREAM_CPPFLAGS := -Iinclude
CAMSTREAM_CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic

TARGET := camstream-capture
HEADERS := $(wildcard include/camstream/*.hpp)
SOURCES := \
	src/main.cpp \
	src/mapped_buffer.cpp \
	src/v4l2_device.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CAMSTREAM_CPPFLAGS) $(CXXFLAGS) $(CAMSTREAM_CXXFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

clean:
	$(RM) $(TARGET)
