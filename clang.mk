CC := clang
SRCDIR := src
OBJDIR := obj-clang
BINDIR := bin-clang
DIRMAIN := $(OBJDIR)/main
DIRREL := $(OBJDIR)/release
DIRTEST := $(OBJDIR)/test
DIRCODECOV := $(OBJDIR)/codecov

INCLUDE := -Iinclude/ -Igenerated/include/

MEMSET := -DUSE_MEMSET
DIAGNO := -DCOLLECT_DIAGNOSTICS

DEBUGFLAG := -g3
TESTFLAGS := $(DEBUGFLAG) -DBUILD_TESTS $(MEMSET)
CODECOVFLAGS := $(DEBUGFLAG) $(TESTFLAGS) --coverage
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
OBJCODECOV := $(patsubst %.c,$(DIRCODECOV)/%.o,$(SRCTEST))
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

$(DIRCODECOV)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo Compiling $< for code coverage..
	@$(CC) -c $(INCLUDE) -o $@ $< $(CFLAGS) $(CODECOVFLAGS)

$(DIRREL)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo Compiling $< for release..
	@$(CC) -c $(INCLUDE) -o $@ $< $(CFLAGS) $(RELFLAGS)

.PHONY: all
all: main release test codecoverage

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

codecoverage: $(VERSIONNOGIT) $(OBJCODECOV)
	@mkdir -p ${BINDIR}
	@$(CC) $(CFLAGS) $(CODECOVFLAGS) $(OBJCODECOV) -o ./${BINDIR}/$@.real

.PHONY: clean
clean:
	@rm -rf ./generated/
	@rm -rf ./$(OBJDIR)
	@rm -rf ./${BINDIR}
