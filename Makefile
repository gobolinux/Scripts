
PYTHON_VERSION=3.8
PYTHON_LIBS=FindPackage GetAvailable GuessLatest CheckDependencies DescribeProgram UseFlags Corrections
PYTHON_SITE=lib/python$(PYTHON_VERSION)/site-packages
PROGRAM=Scripts
VERSION=git-$(shell date +%Y%m%d)
goboPrograms?=/Programs
PREFIX?=
DESTDIR=$(goboPrograms)/$(PROGRAM)/$(VERSION)

exec_files = $(patsubst src/%.c,bin/%,$(wildcard src/*.c))
lib_files = $(patsubst src/lib/%.c,lib/%.so,$(wildcard src/lib/*.c))
man_files = $(shell cd bin; grep -l Parse_Options * | xargs -i echo share/man/man1/{}.1)

all: python $(exec_files) $(lib_files) manuals

debug: python
	cd src; $(MAKE) debug

python:
	for f in $(PYTHON_LIBS); \
	do libf=$(PYTHON_SITE)/$$f.py; \
	   rm -f $$libf; ln -nfs ../../../bin/$$f $$libf; \
	done
	cd $(PYTHON_SITE) && \
	for f in *.py; \
	do python3 -c "import `basename $$f .py`"; \
	done

clean:
	rm -rf Resources/FileHash*
	find * -path "*~" -or -path "*/.\#*" -or -path "*.bak" | xargs rm -f
	cd src && $(MAKE) clean
	cd $(PYTHON_SITE) && rm -f *.pyc *.pyo
	rm -f $(exec_files)
	rm -f $(lib_files)
	rm -rf share/man/man1

manuals: $(man_files)

$(exec_files): bin/%: src/%
	cp -af $< $@
	chmod a+x $@

$(lib_files): lib/%.so: src/lib/%.so
	cp -af $< $@

$(man_files): share/man/man1/%.1: bin/%
	@mkdir -p share/man/man1
	help2man --name=" " --source="GoboLinux" --no-info $< --output $@

src/%: src/%.c
	$(MAKE) -C src

.PHONY: all python manuals debug clean

install: python $(exec_files) manuals
	@echo "Installing Scripts into $(PREFIX)/$(DESTDIR)"
	@install -d  -m 755 $(PREFIX)/$(DESTDIR)
	@echo "Copying files ..."
	@./bin/ListProgramFiles `pwd` | grep -v "^src" | grep -v "^Makefile" | \
	cpio --pass-through --quiet --verbose --unconditional $(PREFIX)/$(DESTDIR)
