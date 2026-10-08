CXX      = g++
CXXFLAGS = -pipe -std=c++17 -Wall -Wextra -O2 -g
SDL_CFLAGS = -IC:/Users/user/msys64/ucrt64/include/SDL2 -Dmain=SDL_main
SDL_LIBS   = -LC:/Users/user/msys64/ucrt64/lib -lmingw32 -lSDL2main -lSDL2

# JIT host codegen backend: x64 (full), arm64|x86 (stub — interpreter only at runtime).
JIT_HOST ?= x64

ifeq ($(JIT_HOST),x64)
  JIT_HOST_FLAG = -DJIT_HOST_X64
  JIT_EMIT_HDR  = $(SRCDIR)/jit/x64/emit.h
  JIT_EMIT_SRCS = $(SRCDIR)/jit/x64/emit_alu.cpp \
                  $(SRCDIR)/jit/x64/emit_branch.cpp \
                  $(SRCDIR)/jit/x64/emit_mem.cpp \
                  $(SRCDIR)/jit/x64/emit_cop.cpp \
                  $(SRCDIR)/jit/x64/emit_got.cpp
else ifeq ($(JIT_HOST),arm64)
  JIT_HOST_FLAG = -DJIT_HOST_ARM64
  JIT_EMIT_HDR  = $(SRCDIR)/jit/arm64/emit.h
  JIT_EMIT_SRCS = $(SRCDIR)/jit/emit_stub.cpp $(SRCDIR)/jit/emit_ref.cpp
else ifeq ($(JIT_HOST),x86)
  JIT_HOST_FLAG = -DJIT_HOST_X86
  JIT_EMIT_HDR  = $(SRCDIR)/jit/x86/emit.h
  JIT_EMIT_SRCS = $(SRCDIR)/jit/emit_stub.cpp $(SRCDIR)/jit/emit_ref.cpp
else
  $(error Unknown JIT_HOST=$(JIT_HOST); use x64, arm64, or x86)
endif

JIT_CXXFLAGS = -I$(SRCDIR)/jit/$(JIT_HOST) -I$(SRCDIR)/jit $(JIT_HOST_FLAG)

# Disable built-in compile rules (%.o: %.cpp drops objects in the project root).
MAKEFLAGS += -r
.SUFFIXES:

SRCDIR = src
OBJDIR = obj

JIT_COMMON = $(SRCDIR)/jit/jit.cpp \
             $(SRCDIR)/jit/frontend.cpp \
             $(SRCDIR)/jit/got_dispatch.cpp \
             $(SRCDIR)/jit/jit_test.cpp

SOURCES = $(SRCDIR)/main.cpp \
          $(SRCDIR)/log.cpp \
          $(SRCDIR)/app_parser.cpp \
          $(SRCDIR)/memory.cpp \
          $(SRCDIR)/cpu.cpp \
          $(SRCDIR)/cop0.cpp \
          $(SRCDIR)/syscalls.cpp \
          $(SRCDIR)/mxu.cpp \
          $(SRCDIR)/display.cpp \
          $(SRCDIR)/archive.cpp \
          $(JIT_COMMON) \
          $(JIT_EMIT_SRCS)

OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

TARGET = emulator.exe

.PHONY: all run clean jit-test jit-test-arm64 jit-test-x86

all: $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

HEADERS = $(SRCDIR)/types.h $(SRCDIR)/log.h $(SRCDIR)/syscalls.h $(SRCDIR)/cpu.h \
          $(SRCDIR)/memory.h $(SRCDIR)/cop0.h $(SRCDIR)/mxu.h \
          $(SRCDIR)/jit/jit.h $(SRCDIR)/jit/frontend.h $(JIT_EMIT_HDR) \
          $(SRCDIR)/jit/tbcache.h $(SRCDIR)/jit/host_config.h

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(HEADERS) | $(OBJDIR)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(JIT_CXXFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	TMPDIR=/c/Users/user/AppData/Local/Temp TMP=/c/Users/user/AppData/Local/Temp TEMP=/c/Users/user/AppData/Local/Temp $(CXX) $(CXXFLAGS) -Wl,--stack,8388608 -o $@ $^ $(SDL_LIBS)

run: $(TARGET)
	./$(TARGET) ../7days.app

jit-test: $(TARGET)
	./$(TARGET) --jit-tests

jit-test-arm64:
	$(MAKE) clean JIT_HOST=arm64 $(TARGET)
	./$(TARGET) --jit-tests

jit-test-x86:
	$(MAKE) clean JIT_HOST=x86 $(TARGET)
	./$(TARGET) --jit-tests

clean:
	rm -f $(TARGET) $(OBJECTS) $(OBJDIR)/*.o $(OBJDIR)/jit/x64/*.o $(OBJDIR)/jit/*.o *.o
