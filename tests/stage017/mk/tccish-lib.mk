# 上の階層から $(MAKE) -C で呼ばれる側 (第 3 部の 3 の 1)。
# 下まで降りて命令が見えることを確かめるためだけの短いもの

CC = cc18
OBJS = a.o b.o

all: libx.a

libx.a: $(OBJS)
	ar rcs $@ $^

%.o: %.c
	$(CC) -c $< -o $@

.PHONY: all
