CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc

all: writer

writer: writer.c
	$(CC) -o $@ $^

clean:
	rm -f writer *.o
