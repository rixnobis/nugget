#!/bin/sh
# ./build.sh "<decls>" "<table entries>" <candidate .s/.c files...>
set -e
D="$1"; T="$2"; shift 2
F="-march=mips1 -mabi=32 -EL -fno-pic -mno-abicalls -ffreestanding -nostdlib -fno-builtin -Os -I../../.."
mipsel-none-elf-gcc $F -DQCANDS_DECL="$D" -DQCANDS="$T" -static -Wl,-e,__start -Wl,-Ttext-segment=0x00400000 -o qtest start.s qtest.c ../../lz4.c "$@"
