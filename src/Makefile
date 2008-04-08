
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
dynamic_libs = libDependencies

all: $(dynamic_exec) $(static_exec) $(dynamic_libs)

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

$(dynamic_libs): %: %.c
	$(CC) $(MYFLAGS) -shared -Wl,-soname,$@.so.0.0.0 -o $@.so.0.0.0 $<; \
	ln -sf $@.so.0.0.0 $@.so.0; \
	ln -sf $@.so.0.0.0 $@.so \

debug: MYCFLAGS = -g -DDEBUG
debug: all

static: MYCFLAGS = $(STATIC)
static: all

clean:
	rm -f $(dynamic_exec) $(static_exec) lib*.so lib*.so.* *.o
	cd ../bin; rm -f $(dynamic_exec) $(static_exec)
	$(RM_EXE)

.PHONY: all clean static debug install
