
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

dynamic_exec = SuperUserName IsExecutable usleep LinkOrExpandAll List CommandNotFound FindDependencies
static_exec = RescueSymlinkProgram
dynamic_targets = $(patsubst %,../bin/%,$(dynamic_exec))
static_targets = $(patsubst %,../bin/%,$(static_exec))

# first rule
default: all

.PHONY: install all defult

all: $(dynamic_exec) $(static_exec)
install: $(dynamic_targets) $(static_targets)

../bin/% : %
	cp -f $< $@
	chmod a+x $@

../bin/RescueSymlinkProgram: RescueSymlinkProgram
	cp -f $< $@
	strip $@
	chmod a+x $@

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
	rm -f $(dynamic_exec) $(static_exec) lib*.so lib*.so.* *.o
	cd ../bin; rm -f $(dynamic_exec) $(static_exec)
	$(RM_EXE)

.PHONY: all clean static debug install
