# 我々の libc を我々の OS の上で組む記述 (Stage 17 第 3 部の 1 の完了条件)。
#
# 第 2 部では go.sh に 8 行並べて書いていた。ここでは**型規則と自動変数**
# で書き直す —— 同じことを 1 つの規則で言えることが，make を持った意味
# である。
#
# **作り直しの判定はしないので毎回すべて作る** (docs/stage017-cc.md 9.4)。

CC = cc18
AR = ar

OBJS  = src/string.o src/stdlib.o src/misc15.o
OBJS += posix/sys.o posix/morecore.o posix/stdio.o
OBJS += posix/assert.o posix/dir.o

all: uselibc

%.o: %.c
	$(CC) -c $< -o $@

libc.a: $(OBJS)
	$(AR) rcs $@ $^

uselibc: uselibc.c libc.a
	$(CC) -o $@ $^

.PHONY: all
