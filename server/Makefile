CFLAGS += -g -Wall -Werror -I../aesd-char-driver
CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc

ifeq ($(SANITIZE),1)
	CFLAGS += -fsanitize=address
endif

all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -f aesdsocket

