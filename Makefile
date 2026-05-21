CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
SDL_CFLAGS = -IC:/Users/user/msys64/ucrt64/include/SDL2 -Dmain=SDL_main
SDL_LIBS = -LC:/Users/user/msys64/ucrt64/lib -lmingw32 -lSDL2main -lSDL2

SRCDIR = src
SOURCES = $(SRCDIR)/main.cpp \
          $(SRCDIR)/app_parser.cpp \
          $(SRCDIR)/memory.cpp \
          $(SRCDIR)/cpu.cpp \
          $(SRCDIR)/cop0.cpp \
          $(SRCDIR)/syscalls.cpp \
          $(SRCDIR)/mxu.cpp \
          $(SRCDIR)/display.cpp \
          $(SRCDIR)/archive.cpp

TARGET = emulator.exe

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -o $@ $^ $(SDL_LIBS)

run: $(TARGET)
	./$(TARGET) ../7days.app

clean:
	rm -f $(TARGET)

.PHONY: all run clean
