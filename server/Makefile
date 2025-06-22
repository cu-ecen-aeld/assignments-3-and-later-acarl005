CFLAGS = -g -Wall -Werror
CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc

ifeq ($(SANITIZE),1)
	CFLAGS += -fsanitize=address
endif

all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) aesdsocket.c -o aesdsocket

clean:
	rm -f aesdsocket
