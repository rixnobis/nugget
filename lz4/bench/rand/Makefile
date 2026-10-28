# make SEED=<1..5> COUNT=<n>; bars.h comes from ../lz4fuzz's bars-$(SEED).txt.
SEED ?= 1
COUNT ?= 20000
TARGET = lz4rand-$(SEED)
TYPE = ps-exe
.DEFAULT_GOAL := all

SRCS = \
../../../common/crt0/crt0.s \
main.c \
../cand1.s \
../../lz4.c \
../../../common/syscalls/printf.s \

CPPFLAGS += -I../../.. -DSEED=$(SEED) -DCOUNT=$(COUNT)

main.o: bars.h
bars.h: bars-$(SEED).txt
	awk 'BEGIN{print "static const uint32_t s_bars[][2] = {"} {printf "{%s, %s},\n", $$1, $$2} END{print "};"}' $< > $@

include ../../../common.mk
