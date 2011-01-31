CC=$(HOME)/Applications/agcc
CFLAGS+=-std=c99

all:    piranha

piranha:    piranha.c bstrlib.c bstrlib.h
	$(CC) $(CFLAGS) -Wall -o piranha piranha.c bstrlib.c

.PHONY: clean

clean:
	rm -f piranha

