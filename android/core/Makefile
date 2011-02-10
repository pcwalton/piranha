include Makefile.config

TARGET=android-8
SYSLIBDIR=$(NDK)/platforms/$(TARGET)/arch-arm/usr/lib

CC=$(NDK)/toolchains/arm-eabi-4.4.0/prebuilt/$(HOST)/bin/arm-eabi-gcc
CFLAGS+=-std=c99 -isystem $(NDK)/platforms/$(TARGET)/arch-arm/usr/include -UNDEBUG
LDFLAGS+=-nostdlib $(SYSLIBDIR)/crtbegin_dynamic.o $(SYSLIBDIR)/crtend_android.o -L$(SYSLIBDIR) -lc -ldl

all:    piranha

piranha:    piranha.c bstrlib.c bstrlib.h
	$(CC) $(CFLAGS) $(LDFLAGS) -Wall -o piranha piranha.c bstrlib.c

.PHONY: clean

clean:
	rm -f piranha

