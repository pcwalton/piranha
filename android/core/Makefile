CC=$(HOME)/Applications/agcc
CFLAGS+=-std=c99 -UNDEBUG

all:    piranha

piranha:    piranha.c bstrlib.c bstrlib.h
	$(CC) $(CFLAGS) $(LDFLAGS) -Wall -o piranha piranha.c bstrlib.c

.PHONY: clean

clean:
	rm -f piranha

