SHELL := bash
PROG := tpm-gadget

OUTDIRS := bin/debug bin/rel

PROG_REL := bin/rel/$(PROG)
PROG_DEBUG := bin/debug/$(PROG)

SRCFILES := $(wildcard src/*.c)

OBJFILES_REL := $(patsubst src/%.c,bin/rel/%.o,$(SRCFILES))
OBJFILES_DEBUG := $(patsubst src/%.c,bin/debug/%.o,$(SRCFILES))

DEPFILES := $(patsubst src/%.c,bin/%.d,$(SRCFILES))

CFLAGS := -Wall -MMD -MP -fpermissive
LFLAGS := -lpthread -lrt
DBFLAGS := -g
RELFLAGS := 

CC := gcc

.PHONY: default all testmake debug release clean dirs

default: debug 

all:    dirs clean debug release

dirs: 
	@mkdir -p  $(OUTDIRS)

debug:  $(PROG_DEBUG)

release: $(PROG_REL)

testmake:
	@echo OBJFILES_REL = $(OBJFILES_REL)
	@echo OBJFILES_DEBUG = $(OBJFILES_DEBUG)
	@echo SRCFILES = $(SRCFILES)
	@echo DEPFILES = $(DEPFILES)

clean:
	rm -f $(OBJFILES_REL) $(OBJFILES_DEBUG) $(DEPFILES) $(PROG)

$(PROG_REL): $(OBJFILES_REL)
	$(CC)  $(LFLAGS) $(OBJFILES_REL) -o $(PROG_REL)
	strip $(PROG_REL)
	@echo "----  created release binary ----"


$(PROG_DEBUG): $(OBJFILES_DEBUG)
	$(CC) $(LFLAGS) $(OBJFILES_DEBUG) -o $(PROG_DEBUG)
	@echo "----  created debug binary ----"

-include $(DEPFILES)

bin/rel/%.o: src/%.c
	$(CC) $(RELFLAGS) $(CFLAGS) -MF $(patsubst bin/rel/%.o, bin/%.d,$@) -c $< -o $@

bin/debug/%.o: src/%.c
	$(CC) $(DBFLAGS) $(CFLAGS) -MF $(patsubst bin/debug/%.o, bin/%.d,$@) -c $< -o $@
