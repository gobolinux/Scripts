
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

dynamic_exec = SuperUserName IsExecutable usleep LinkOrExpandAll List CommandNotFound FindDependencies GetSupportedFilesystems
static_exec = RescueSymlinkProgram

# first rule
default: all

.PHONY: all defult

all: $(dynamic_exec) $(static_exec)

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

debug: MYCFLAGS = -g -DDEBUG -Wall
debug: all

static: MYCFLAGS = $(STATIC)
static: all

clean:
	rm -f $(dynamic_exec) $(static_exec) lib*.so lib*.so.* *.o
	$(RM_EXE)

.PHONY: all clean static debug install
