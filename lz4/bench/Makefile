TARGET = lz4duel
TYPE = ps-exe
.DEFAULT_GOAL := all

# corpus.bin comes from mkcorpus.py (python lz4 module) and the host build of
# ../lz4.c: make lz4host && python3 mkcorpus.py
SRCS = \
../../common/crt0/crt0.s \
main.c \
sled.s \
corpus.s \
../lz4.c \
../../common/syscalls/printf.s \
$(CANDIDATE_SRCS) \

CPPFLAGS += -I../..
CPPFLAGS += $(CANDIDATE_FLAGS)

corpus.o: corpus.bin

lz4host: lz4host.c ../lz4.c ../lz4.h
	gcc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o $@ lz4host.c ../lz4.c

include ../../common.mk
