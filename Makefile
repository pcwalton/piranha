CC=$(HOME)/Applications/agcc
CFLAGS+=-std=c99

all:    piranha

piranha:    piranha.c
	$(CC) $(CFLAGS) -Wall -o piranha piranha.c

.PHONY: clean

clean:
	rm -f piranha

