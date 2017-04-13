DESTDIR?=
CC = gcc
CFLAGS = -O2 -Wall
INSTALL_FILE=install -s
INSTALL_DIR=install -d

# Running on windows
IS_CYGWIN=$(shell uname | grep -i cygwin &>/dev/null && echo yes || echo no)
ifeq ($(IS_CYGWIN),yes)
   RM_EXE=-rm -f *.exe
endif

STATIC_CFLAGS = $(CFLAGS) -static
ifneq ($(shell uname -s),Darwin)
	STATIC_CFLAGS = $(CFLAGS) 
endif

# Add compiler options
ifdef DEBUG
	CFLAGS += -g -DDEBUG
	STATIC_CFLAGS += -g -DDEBUG
	INSTALL_FILE=install
endif

exec_files = $(patsubst %.c,%,$(wildcard *.c))

dynamic_exec = BackgroundExec SuperUserName IsExecutable usleep LinkOrExpandAll List CommandNotFound GetSupportedFilesystems
static_exec = RescueSymlinkProgram
other_exec = FindDependencies Runner

# first rule

.PHONY: all clean static debug install

all: $(dynamic_exec) $(static_exec) $(other_exec)

$(dynamic_exec): %: %.c
	@echo "Compiling $@"
	$(CC) $(CFLAGS) $< -o $@
	if [ -e "$@.exe" ]; then \
		mv $@.exe $@; \
	fi

$(static_exec): %: %.c
	@echo "Compiling $@"
	$(CC) $(STATIC_CFLAGS) $< -o $@
	if [ -e "$@.exe" ]; then \
		mv $@.exe $@; \
	fi

FindDependencies: %: %.c
	@echo "Compiling $@"
	$(CC) $(CFLAGS) $< -o $@ -DBUILD_MAIN

Runner: Runner.c FindDependencies.c
	@echo "Compiling $@"
	$(CC) $(CFLAGS) $^ -o $@

clean:
	@echo "Cleaning sources"
	rm -f $(dynamic_exec) $(static_exec) $(other_exec) lib*.so lib*.so.* *.o
	$(RM_EXE)

install: all
	@echo "Installing compiled binaries"
ifndef DESTDIR
	$(error DESTDIR is not set)
endif
	$(INSTALL_DIR) -d -m 755 $(DESTDIR)/bin
	$(foreach EXEC_FILE, $(exec_files), \
		$(INSTALL_FILE) -m 755 $(EXEC_FILE) $(DESTDIR)/bin ; \
	)
	chmod 4755 $(DESTDIR)/bin/Runner

