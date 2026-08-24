CXX      = g++
CXXFLAGS = -pipe -std=c++17 -Wall -Wextra -O2 -g
SDL_CFLAGS = -IC:/Users/user/msys64/ucrt64/include/SDL2 -Dmain=SDL_main
SDL_LIBS   = -LC:/Users/user/msys64/ucrt64/lib -lmingw32 -lSDL2main -lSDL2

# Disable built-in compile rules (%.o: %.cpp drops objects in the project root).
MAKEFLAGS += -r
.SUFFIXES:

SRCDIR = src
OBJDIR = obj

SOURCES = $(SRCDIR)/main.cpp \
          $(SRCDIR)/app_parser.cpp \
          $(SRCDIR)/memory.cpp \
          $(SRCDIR)/cpu.cpp \
          $(SRCDIR)/cop0.cpp \
          $(SRCDIR)/syscalls.cpp \
          $(SRCDIR)/mxu.cpp \
          $(SRCDIR)/display.cpp \
          $(SRCDIR)/archive.cpp

OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

TARGET = emulator.exe

.PHONY: all run clean

all: $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(SRCDIR)/types.h $(SRCDIR)/syscalls.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c $< -o $(OBJDIR)/$*.o

$(TARGET): $(OBJECTS)
	TMPDIR=/c/Users/user/AppData/Local/Temp TMP=/c/Users/user/AppData/Local/Temp TEMP=/c/Users/user/AppData/Local/Temp $(CXX) $(CXXFLAGS) -Wl,--stack,8388608 -o $@ $^ $(SDL_LIBS)

run: $(TARGET)
	./$(TARGET) ../7days.app

clean:
	rm -f $(TARGET) $(OBJDIR)/*.o *.o
