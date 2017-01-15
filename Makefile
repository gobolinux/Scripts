PROGRAM=Scripts
VERSION=git-$(shell date +%Y%m%d)
goboPrograms?=/Programs
PREFIX?=
DESTDIR=$(PREFIX)/$(goboPrograms)/$(PROGRAM)/$(VERSION)

INSTALL_FILE = install
INSTALL_DIR = install -d

PYTHON_VERSION=2.7
PYTHON_LIBS=FindPackage GetAvailable GuessLatest CheckDependencies DescribeProgram UseFlags Corrections
PYTHON_SITE=python$(PYTHON_VERSION)/site-packages

RUBY_VERSION=1.8
RUBY_SITE=ruby/site_ruby/$(RUBY_VERSION)
RUBY_DIR=$(RUBY_SITE)/gobo

MAN_FILES = $(shell cd bin;grep -l Parse_Options * | xargs -i echo share/man/man1/{}.1)
EXEC_FILES = $(patsubst src/%.c,%,$(wildcard src/*.c))
SCRIPT_FILES = AddUser AttachProgram DeduceName FindPackage GoboPath install PrioritiseUpdates ScriptFunctions UnversionExecutables VersionExecutables Alien AugmentCommandNotFoundDatabase Dependencies FindQuick GrepQuick InstallPackage ProblemReport SignProgram UpdateKdeRecipe which Alien-Cabal CheckDependants DescribeProgram FixAttributes GrepReplace KillProcess RemoveBroken SuggestDuplicates UpdateSettings xmlcatalog Alien-CPAN CheckDependencies DetachProgram FixDirReferences GuessLatest ListProgramFiles RemoveEmpty SuggestUpdates UpdateXorgRecipe Alien-LuaRocks CleanModules DisableProgram FixInfo GuessProgramCase RemoveProgram SymlinkProgram UpgradeSystem Alien-PIP Corrections FilterColors GenBuildInformation HasCompatiblePackage MergeTree Rename SystemFind UseFlags Alien-RubyGems CreatePackage FilterLines GetAvailable Hashes NamingConventions RescueInstallPackage SystemInfo VerifyProgram

.PHONY: all clean install

all: python manuals
	@$(MAKE) -C src
	$(foreach EXE_FILE, $(EXEC_FILES), \
		cp -af src/$(EXE_FILE) bin ; \
		chmod a+x bin/$(EXE_FILE) ; \
	)

python:
	mkdir -p lib/$(PYTHON_SITE)
	$(foreach PYTHON_LIB, $(PYTHON_LIBS), \
		ln -nfs ../../../bin/$(PYTHON_LIB) lib/$(PYTHON_SITE)/$(PYTHON_LIB).py ; \
	)
	cp lib/python/*.py lib/$(PYTHON_SITE) 
	cd lib/$(PYTHON_SITE) && \
	for f in *.py; \
	do  python -c "import `basename $$f .py`"; \
	done

python_clean:
	rm -rf lib/$(dir $(PYTHON_SITE))

clean: python_clean
	@$(MAKE) -C src clean
	@echo "Cleaning man pages"
	rm -rf share/man/man1
	@echo "Cleaning binaries"
	$(foreach EXE_FILE, $(EXEC_FILES), \
		rm -f src/$(EXE_FILE) ; \
		rm -f bin/$(EXE_FILE) ; \
	)
	rm -rf Resources/FileHash*

python_install: python
	@echo "Installing python libraries"
	mkdir -p $(DESTDIR)/lib/$(dir $(PYTHON_SITE))
	cp -r lib/$(dir $(PYTHON_SITE)) $(DESTDIR)/lib

ruby_install:
	@echo "Installing ruby libraries"
	$(INSTALL_DIR) -d -m 755 $(DESTDIR)/lib/$(RUBY_SITE)
	cp -r lib/$(RUBY_DIR) $(DESTDIR)/lib/$(RUBY_SITE)

$(MAN_FILES): share/man/man1/%.1: bin/%
	@mkdir -p share/man/man1
	help2man --name=" " --source="GoboLinux" --no-info $< --output $@

manuals: $(MAN_FILES)

install_manuals: $(MAN_FILES)
	$(INSTALL_DIR) -d -m 755 $(DESTDIR)/share/man/man1
	$(foreach MAN_FILE, $(MAN_FILES), \
		$(INSTALL_FILE) -m 644 $(MAN_FILE) $(DESTDIR)/share/man/man1 ; \
	)

install_scripts:
	@echo "Installing scripts"
	$(INSTALL_DIR) -d -m 755 $(DESTDIR)/bin
	$(foreach SCRIPT_FILE, $(SCRIPT_FILES), \
	   	$(INSTALL_FILE) -m 755 bin/$(SCRIPT_FILE) $(DESTDIR)/bin ; \
	)

install_data:
	@echo "Installing Data"
	$(INSTALL_DIR) -d -m 755 $(DESTDIR)/Data 
	cp -r Data $(DESTDIR)

install_resources:
	@echo "Installing Resources"
	cp -r Resources $(DESTDIR) 
	
install_share_data:
	@echo "Installing share data"
	cp -rf share $(DESTDIR)

install_functions:
	@echo "Installing Functions"
	cp -rf Functions $(DESTDIR)

prepare_install:
	@echo "Installing $(PROGRAM) into $(DESTDIR)"
	$(INSTALL_DIR) -m 755 $(DESTDIR)

install: all prepare_install install_scripts install_data install_resources install_share_data install_functions python_install ruby_install install_manuals
	@$(MAKE) DESTDIR=$(DESTDIR) -C src install
	@echo "Installed $(PROGRAM) into $(DESTDIR)"
