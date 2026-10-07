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
          $(SRCDIR)/archive.cpp \
          $(SRCDIR)/jit/jit.cpp \
          $(SRCDIR)/jit/frontend.cpp \
          $(SRCDIR)/jit/emit_alu.cpp \
          $(SRCDIR)/jit/emit_branch.cpp \
          $(SRCDIR)/jit/emit_mem.cpp \
          $(SRCDIR)/jit/emit_cop.cpp \
          $(SRCDIR)/jit/jit_test.cpp

OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

TARGET = emulator.exe

.PHONY: all run clean

all: $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Header dependencies (must be complete: jit.h/cpu.h change class layouts;
# a stale object file with an old sizeof() smashes its stack neighbours).
HEADERS = $(SRCDIR)/types.h $(SRCDIR)/syscalls.h $(SRCDIR)/cpu.h \
          $(SRCDIR)/memory.h $(SRCDIR)/cop0.h $(SRCDIR)/mxu.h \
          $(SRCDIR)/jit/jit.h $(SRCDIR)/jit/frontend.h $(SRCDIR)/jit/emit.h \
          $(SRCDIR)/jit/tbcache.h

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(HEADERS) | $(OBJDIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c $< -o $@

# Nested obj dirs (src/jit/*.cpp -> obj/jit/*.o): the single-% rule above
# does not match paths with extra slashes, so spell these out.
$(OBJDIR)/jit/%.o: $(SRCDIR)/jit/%.cpp $(HEADERS) | $(OBJDIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	TMPDIR=/c/Users/user/AppData/Local/Temp TMP=/c/Users/user/AppData/Local/Temp TEMP=/c/Users/user/AppData/Local/Temp $(CXX) $(CXXFLAGS) -Wl,--stack,8388608 -o $@ $^ $(SDL_LIBS)

run: $(TARGET)
	./$(TARGET) ../7days.app

clean:
	rm -f $(TARGET) $(OBJECTS) $(OBJDIR)/*.o *.o
