
MYCFLAGS = -O2 -Wall 
CC = gcc

STATIC=-static
ifeq ($(shell uname -s),Darwin)
   STATIC=
endif

IS_CYGWIN=$(shell uname | grep -i cygwin &>/dev/null && echo yes || echo no)
ifeq ($(IS_CYGWIN),yes)
   RM_EXE=-rm -f *.exe
endif

dynamic_exec = BackgroundExec SuperUserName IsExecutable usleep LinkOrExpandAll List CommandNotFound GetSupportedFilesystems
static_exec = RescueSymlinkProgram
other_exec = FindDependencies Runner
dynamic_lib = lib/RunnerRedirect.so lib/DynamicLoaderRedirect.so

# first rule
default: all

.PHONY: all default

all: $(dynamic_exec) $(static_exec) $(other_exec) $(dynamic_lib)

$(dynamic_exec): %: %.c
	$(CC) $(MYCFLAGS) $< -o $@
	@if [ -e "$@.exe" ]; then \
		mv $@.exe $@; \
	fi

$(static_exec): %: %.c
	$(CC) $(MYCFLAGS) $< -o $@ $(STATIC)
	@if [ -e "$@.exe" ]; then \
		mv $@.exe $@; \
	fi

FindDependencies: %: %.c
	$(CC) $(MYCFLAGS) $< -o $@ -DBUILD_MAIN

Runner: Runner.c FindDependencies.c
	$(CC) $(MYCFLAGS) $^ -o $@
	chmod 4755 $@

$(dynamic_lib): lib/%.so: lib/%.c
	$(CC) -shared -fpic -ldl $< -o $@

debug: MYCFLAGS = -g -DDEBUG -Wall
debug: all

static: MYCFLAGS = $(STATIC)
static: all

clean:
	rm -f $(dynamic_exec) $(static_exec) $(other_exec) $(dynamic_lib) lib*.so lib*.so.* *.o
	$(RM_EXE)

.PHONY: all clean static debug install
