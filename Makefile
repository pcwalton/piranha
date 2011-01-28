CC=$(HOME)/Applications/agcc

all:    piranha

piranha:    piranha.c
	$(CC) $(CFLAGS) -Wall -o piranha piranha.c

