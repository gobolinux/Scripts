
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

dynamic_exec = SuperUserName IsExecutable usleep LinkOrExpandAll List CommandNotFound
static_exec = RescueSymlinkProgram

all: $(dynamic_exec) $(static_exec)

install:
	strip RescueSymlinkProgram
	mv $(dynamic_exec) $(static_exec) ../bin
	chmod a+x ../bin/*

$(dynamic_exec): %: %.c
	$(CC) $(MYFLAGS) $< -o $@
	@if [ -e "$@.exe" ]; then \
		mv $@.exe $@; \
	fi

$(static_exec): %: %.c
	$(CC) $(MYFLAGS) $< -o $@ $(STATIC)
	@if [ -e "$@.exe" ]; then \
		mv $@.exe $@; \
	fi

debug: MYCFLAGS = -g -DDEBUG
debug: all

static: MYCFLAGS = $(STATIC)
static: all

clean:
	rm -f $(dynamic_exec) $(static_exec)
	cd ../bin; rm -f $(dynamic_exec) $(static_exec)
	$(RM_EXE)

.PHONY: all clean static debug install
