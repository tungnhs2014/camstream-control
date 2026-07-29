CXX ?= g++
PKG_CONFIG ?= pkg-config
CPPFLAGS ?=
CXXFLAGS ?= -O2
LDFLAGS ?=
LDLIBS ?=

CAMSTREAM_CPPFLAGS := -Iinclude
CAMSTREAM_CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic
GSTREAMER_CPPFLAGS := $(shell $(PKG_CONFIG) --cflags gstreamer-1.0 2>/dev/null)
GSTREAMER_LDLIBS := $(shell $(PKG_CONFIG) --libs gstreamer-1.0 2>/dev/null)

TARGET := camstream-gst-test
HEADERS := $(wildcard include/camstream/*.hpp)
SOURCES := \
	src/main.cpp \
	src/gstreamer_pipeline.cpp

.PHONY: all check-dependencies clean

all: $(TARGET)

check-dependencies:
	@$(PKG_CONFIG) --exists gstreamer-1.0 || { \
		echo "Error: target gstreamer-1.0 pkg-config metadata is unavailable" >&2; \
		exit 1; \
	}

$(TARGET): $(SOURCES) $(HEADERS) | check-dependencies
	$(CXX) $(CPPFLAGS) $(CAMSTREAM_CPPFLAGS) $(GSTREAMER_CPPFLAGS) \
		$(CXXFLAGS) $(CAMSTREAM_CXXFLAGS) $(LDFLAGS) -o $@ $(SOURCES) \
		$(LDLIBS) $(GSTREAMER_LDLIBS)

clean:
	$(RM) $(TARGET)
