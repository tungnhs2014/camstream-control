CXX ?= g++
CPPFLAGS ?=
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?=

TARGET := camstream-capture
HEADERS := $(wildcard include/camstream/*.hpp)
SOURCES := \
	src/main.cpp \
	src/mapped_buffer.cpp \
	src/v4l2_device.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

clean:
	$(RM) $(TARGET)
