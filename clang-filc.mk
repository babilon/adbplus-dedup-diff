# download fil-c tarball, extract, run ./setup.sh, and link the filc directory
# to ./filc-bin
#
# https://github.com/pizlonator/fil-c/blob/deluge/Manifesto.md#using-a-fil-c-release
CC := ./filc-bin/build/bin/clang
SRCDIR := src
OBJDIR := obj-clang-filc
BINDIR := bin-clang-filc
DIRMAIN := $(OBJDIR)/main
DIRREL := $(OBJDIR)/release
DIRTEST := $(OBJDIR)/test

INCLUDE := -Iinclude/ -Igenerated/include/

MEMSET := -DUSE_MEMSET
DIAGNO := -DCOLLECT_DIAGNOSTICS

DEBUGFLAG := -g3
TESTFLAGS := $(DEBUGFLAG) -DBUILD_TESTS $(MEMSET)
MAINFLAGS := $(DEBUGFLAG) $(DIAGNO)
RELFLAGS := -O3 -DRELEASE_LOGGING -DCOLLECT_DIAGNOSTICS -DNDEBUG

CFLAGS := -std=c23 -Wall -Wextra -Werror
LFLAGS := -std=c23 -Wall -Wextra -Werror

VERSIONDOTH := include/version.h
SRC ?=
SRCTEST ?= $(SRC)

include src/Submakefile src/tests/Submakefile

OBJREL := $(patsubst %.c,$(DIRREL)/%.o,$(SRC))
OBJMAIN := $(patsubst %.c,$(DIRMAIN)/%.o,$(SRC))
OBJTEST := $(patsubst %.c,$(DIRTEST)/%.o,$(SRCTEST))
VERSIONNOGIT := $(patsubst %.h,generated/%.nogit.h,$(VERSIONDOTH))

generated/%.nogit.h: $(VERSIONDOTH) createversion.sh
	/bin/sh ./createversion.sh

$(DIRMAIN)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo Using $(CC) to compile $< for main debug..
	@$(CC) -c $(INCLUDE) -o $@ $< $(CFLAGS) $(MAINFLAGS)

$(DIRTEST)/%.o: %.c
	@echo $@
	@mkdir -p $(dir $@)
	@echo Compiling $< for unit testing..
	@$(CC) -c $(INCLUDE) -o $@ $< $(CFLAGS) $(TESTFLAGS)

$(DIRREL)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo Compiling $< for release..
	@$(CC) -c $(INCLUDE) -o $@ $< $(CFLAGS) $(RELFLAGS)

.PHONY: all
all: main release test

main: $(VERSIONNOGIT) $(OBJMAIN)
	@echo Linking $@
	@mkdir -p ${BINDIR}
	@$(CC) $(LFLAGS) $(OBJMAIN) -o ./${BINDIR}/$@.real

release: $(VERSIONNOGIT) $(OBJREL)
	@echo Linking $@
	@mkdir -p ${BINDIR}
	@$(CC) $(FLAGS) $(OBJREL) -o ./${BINDIR}/$@.real

fpos: obj/testfpos.o
	@mkdir -p ${BINDIR}
	@$(CC) $(LFLAGS) $^ -o ./${BINDIR}/$@.real

test: $(VERSIONNOGIT) $(OBJTEST)
	@mkdir -p ${BINDIR}
	@$(CC) $(CFLAGS) $(TESTFLAGS) $(OBJTEST) -o ./${BINDIR}/$@.real

.PHONY: clean
clean:
	@rm -rf ./generated/
	@rm -rf ./$(OBJDIR)
	@rm -rf ./${BINDIR}
