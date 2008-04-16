
VERSION=
PROGRAM=Scripts
PACKAGE_DIR=$(HOME)
PACKAGE_FILE=$(PACKAGE_DIR)/$(PROGRAM)--$(VERSION)--$(shell uname -m).tar.bz2
TARBALL_BASE=$(PROGRAM)-$(VERSION)
TARBALL_ROOT=$(PACKAGE_DIR)/$(TARBALL_BASE)
TARBALL_FILE=$(PACKAGE_DIR)/$(PROGRAM)-$(VERSION).tar.gz
DESTDIR=/Programs/Scripts/$(VERSION)/
SVNTAG=`echo $(PROGRAM)_$(VERSION) | tr "[:lower:]" "[:upper:]" | sed  's,\.,_,g'`

PYTHON_VERSION=2.3
PYTHON_LIBS=FindPackage GetAvailable GuessLatest CheckDependencies DescribeProgram UseFlags
PYTHON_SITE=lib/python$(PYTHON_VERSION)/site-packages

default: all
	cd src; make install

all: python
	cd src; make all

debug: python
	cd src; make debug

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
	@[ "$(VERSION)" = "" ] && { echo -e "Error: run make with VERSION=<version-number>.\n"; exit 1 ;} || exit 0

clean: cleanup

cleanup:
	rm -rf Resources/FileHash*
	find * -path "*~" -or -path "*/.\#*" -or -path "*.bak" | xargs rm -f
	cd src && make clean
	cd $(PYTHON_SITE) && rm -f *.pyc *.pyo

verify:
	@svn update
	@{ svn status 2>&1 | grep -v "Resources/SettingsBackup" | grep "^\?" ;} && { echo -e "Error: unknown files exist. Please take care of them first.\n"; exit 1 ;} || exit 0
	@{ svn status 2>&1 | grep "^M" ;} && { echo -e "Error: modified files exist. Please checkin/revert them first.\n"; exit 1 ;}

dist: version_check cleanup verify default
	sed -i "s/CURRENT_SCRIPTS_VERSION=.*#/CURRENT_SCRIPTS_VERSION="${VERSION}" #/g" bin/CreateRootlessEnvironment
	svn commit -m "Update version." bin/CreateRootlessEnvironment
	rm -rf $(PACKAGE_DIR)/$(PROGRAM)/$(VERSION)
	mkdir -p $(PACKAGE_DIR)/$(PROGRAM)/$(VERSION)
	ListProgramFiles $(shell pwd) | cpio -p $(PACKAGE_DIR)/$(PROGRAM)/$(VERSION)
	cd $(PACKAGE_DIR); tar cvp $(PROGRAM)/$(VERSION) | bzip2 > $(PACKAGE_FILE)
	rm -rf $(PACKAGE_DIR)/$(PROGRAM)/$(VERSION)
	rmdir $(PACKAGE_DIR)/$(PROGRAM)
	SignProgram $(PACKAGE_FILE)
	@echo; echo "Package at $(PACKAGE_FILE)"
	@echo; echo "Now make a tag by running \`svn cp http://svn.gobolinux.org/tools/trunk/$(PROGRAM) http://svn.gobolinux.org/tools/tags/$(SVNTAG) -m"Tagging $(PROGRAM) $(VERSION)\`"

tarball: version_check cleanup
	rm -rf $(TARBALL_ROOT)
	mkdir -p $(TARBALL_ROOT)
	ListProgramFiles $(PROGRAM) | cpio -p $(TARBALL_ROOT)
	cd $(TARBALL_ROOT) && sed -i "s,^VERSION=,VERSION=$(VERSION)," Makefile
	cd $(PACKAGE_DIR); tar cvp $(TARBALL_BASE) | gzip > $(TARBALL_FILE)
	rm -rf $(TARBALL_ROOT)
	@echo; echo "Tarball at $(TARBALL_FILE)"; echo
	make all

install: version_check
	cp -a * $(DESTDIR)

manuals:
	mkdir -p man/man1
	for i in `cd bin && grep -l Parse_Options *`; do bn=`basename $$i`; help2man --name=" " --source="GoboLinux" --no-info $$bn > man/man1/$$bn.1; done

