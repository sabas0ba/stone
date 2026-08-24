# 関数を使って libc を組む記述 (Stage 17 第 3 部の 2 の完了条件)。
#
# 第 3 部の 1 では目的ファイルを 8 個手で並べていた。ここでは
# **元のファイルの並びから関数で導く** —— 同じことを言うのに
# 手で並べ直さなくてよくなるのが，関数を持った意味である。

CC = cc18
AR = ar

SRC = src/string.c src/stdlib.c src/misc15.c
SRC += posix/sys.c posix/morecore.c posix/stdio.c
SRC += posix/assert.c posix/dir.c

# $(patsubst) で .c -> .o を導く。手で並べない
OBJS = $(patsubst %.c,%.o,$(SRC))

# $(filter) と $(foreach) と $(call) も通しておく
POSIX = $(filter posix/%,$(SRC))
tagged = <$(1)>
TAGS = $(foreach f,$(POSIX),$(call tagged,$(f)))

all: uselibc

%.o: %.c
	$(CC) -c $< -o $@

libc.a: $(OBJS)
	$(AR) rcs $@ $^

uselibc: uselibc.c libc.a
	$(CC) -o $@ $^

show:
	@echo "objs $(words)$(OBJS)"
	@echo "posix $(POSIX)"
	@echo "tags $(TAGS)"
	@echo "first $(firstword $(SRC))"
	@echo "sub $(subst posix/,P/,posix/dir.c)"
	@echo "if $(if $(OBJS),have,none)"

.PHONY: all show
