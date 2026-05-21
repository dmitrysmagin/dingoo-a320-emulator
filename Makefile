CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
LDFLAGS = $(shell sdl2-config --libs)
CXXFLAGS += $(shell sdl2-config --cflags)

SRCDIR = src
SOURCES = $(SRCDIR)/main.cpp \
          $(SRCDIR)/app_parser.cpp \
          $(SRCDIR)/memory.cpp \
          $(SRCDIR)/cpu.cpp \
          $(SRCDIR)/cop0.cpp \
          $(SRCDIR)/syscalls.cpp \
          $(SRCDIR)/mxu.cpp \
          $(SRCDIR)/display.cpp

TARGET = emulator.exe

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) ../7days.app

clean:
	rm -f $(TARGET)

.PHONY: all run clean
