
PROGRAM=Scripts
VERSION=svn-$(shell date +%Y%m%d)
PACKAGE_DIR=$(HOME)
PACKAGE_FILE=$(PACKAGE_DIR)/$(PROGRAM)--$(VERSION)--$(shell uname -m).tar.bz2
TARBALL_BASE=$(PROGRAM)-$(VERSION)
TARBALL_ROOT=$(PACKAGE_DIR)/$(TARBALL_BASE)
TARBALL_FILE=$(PACKAGE_DIR)/$(PROGRAM)-$(VERSION).tar.gz
goboPrograms ?= /Programs
PREFIX=
DESTDIR=$(goboPrograms)/$(PROGRAM)/$(VERSION)
SVNTAG:=$(shell echo $(PROGRAM)_$(VERSION) | tr "[:lower:]" "[:upper:]" | sed  's,\.,_,g')

PYTHON_VERSION=2.3
PYTHON_LIBS=FindPackage GetAvailable GuessLatest CheckDependencies DescribeProgram UseFlags
PYTHON_SITE=lib/python$(PYTHON_VERSION)/site-packages

ifeq (,$(findstring svn,$(VERSION)))
INSTALL_TARGET=install-files
else
INSTALL_TARGET=install-svn
endif

all_files = $(shell ListProgramFiles `pwd`)
man_files = $(shell cd bin; grep -l Parse_Options * | xargs -i echo Shared/man/man1/{}.1)
exec_files = $(patsubst src/%.c,bin/%,$(wildcard src/*.c))

default: all

all: python $(exec_files) manuals

debug: python
	cd src; $(MAKE) debug

python:
	for f in $(PYTHON_LIBS); \
	do libf=$(PYTHON_SITE)/$$f.py; \
	   rm -f $$libf; ln -nfs ../../../bin/$$f $$libf; \
	done
	cd $(PYTHON_SITE) && \
	for f in *.py; \
	do python -c "import `basename $$f .py`"; \
	done

version_check:
ifeq (,$(findstring svn,$(VERSION)))
	@[ "$(VERSION)" = "" ] && { echo -e "Error: run make with VERSION=<version-number>.\n"; exit 1 ;} || exit 0
else
	@echo "You must specify a version when you run \"make dist\""
	@exit 1
endif

clean: cleanup

cleanup:
	rm -rf Resources/FileHash*
	find * -path "*~" -or -path "*/.\#*" -or -path "*.bak" | xargs rm -f
	cd src && $(MAKE) clean
	cd $(PYTHON_SITE) && rm -f *.pyc *.pyo
	rm -f $(exec_files)

verify:
	@svn update
	@{ svn status 2>&1 | grep -v "Resources/SettingsBackup" | grep "^\?" ;} && { echo -e "Error: unknown files exist. Please take care of them first.\n"; exit 1 ;} || exit 0
	@{ svn status 2>&1 | grep "^M" ;} && { echo -e "Error: modified files exist. Please checkin/revert them first.\n"; exit 1 ;} || exit 0

update_version: version_check verify
	sed -i "s/CURRENT_SCRIPTS_VERSION=.*#/CURRENT_SCRIPTS_VERSION="${VERSION}" #/g" bin/CreateRootlessEnvironment
	svn commit -m "Update version for CreateRootlessEnvironment." bin/CreateRootlessEnvironment

dist: update_version
	@echo; echo "Press enter to create a subversion tag and tarball for version $(VERSION) or ctrl-c to abort."
	@read
	@$(MAKE) tag

tag: version_check verify
	svn cp http://svn.gobolinux.org/tools/trunk/$(PROGRAM) http://svn.gobolinux.org/tools/tags/$(SVNTAG) -m"Tagging $(PROGRAM) $(VERSION)"
	svn switch http://svn.gobolinux.org/tools/tags/$(SVNTAG)
	sed -i 's/^VERSION=.*/VERSION='"$(VERSION)"'/' Makefile
	svn commit -m"Updating version in Makefile." Makefile
	$(MAKE) tarball
	svn switch http://svn.gobolinux.org/tools/trunk/$(PROGRAM)

tarball: manuals $(PACKAGE_FILE)
	@echo; echo "Tarball at $(PACKAGE_FILE)"

$(PACKAGE_FILE): $(all_files)
	rm -rf $(PACKAGE_DIR)/$(PROGRAM)/$(VERSION)
	mkdir -p $(PACKAGE_DIR)/$(PROGRAM)/$(VERSION)
	@ListProgramFiles $(shell pwd) | cpio -p $(PACKAGE_DIR)/$(PROGRAM)/$(VERSION)
	cd $(PACKAGE_DIR)/$(PROGRAM)/$(VERSION); $(MAKE) clean default
	cd $(PACKAGE_DIR); tar cvp $(PROGRAM)/$(VERSION) | bzip2 > $(PACKAGE_FILE)
	rm -rf $(PACKAGE_DIR)/$(PROGRAM)/$(VERSION)
	rmdir $(PACKAGE_DIR)/$(PROGRAM)
	SignProgram $(PACKAGE_FILE)

manuals: $(man_files)

$(man_files): Shared/man/man1/%.1: bin/%
	@mkdir -p Shared/man/man1
	help2man --name=" " --source="GoboLinux" --no-info $< --output $@

$(exec_files): bin/%: src/%
	cp -f $< $@
	chmod a+x $@

src/%: src/%.c
	$(MAKE) -C src

install: $(INSTALL_TARGET)

install-files: $(exec_files)
	install -d -m 755 $(PREFIX)$(DESTDIR)
	@echo "Copying files"
	@ListProgramFiles `pwd` | cpio --pass-through --quiet --verbose --unconditional $(PREFIX)$(DESTDIR)

install-svn: install-files
	@echo "Installing subversion information"
	@find -name ".svn" -exec echo "$(PREFIX)$(DESTDIR)/{}" \; -exec cp --archive --remove-destination '{}' "$(PREFIX)$(DESTDIR)"/'{}' \;

.PHONY: default all debug python version_check clean cleanup verify update_version

.PHONY: dist tag tarball manuals install install-files install-svn
