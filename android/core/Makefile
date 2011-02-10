include Makefile.config

TARGET=android-8
SYSLIBDIR=$(NDK)/platforms/$(TARGET)/arch-arm/usr/lib
TOOLCHAINDIR=$(NDK)/toolchains/arm-eabi-4.4.0/prebuilt/$(HOST)

CC=$(TOOLCHAINDIR)/bin/arm-eabi-gcc
CFLAGS+=-std=c99 -march=armv5te -mtune=xscale -msoft-float -mthumb-interwork -fpic -fno-exceptions -ffunction-sections -funwind-tables -fstack-protector -fmessage-length=0 -isystem $(NDK)/platforms/$(TARGET)/arch-arm/usr/include -UNDEBUG
LDFLAGS+=-Bdynamic -Wl,-T,$(TOOLCHAINDIR)/arm-eabi/lib/ldscripts/armelf.x -Wl,-dynamic-linker,/system/bin/linker -Wl,--gc-sections -Wl,-z,nocopyreloc -Wl,--no-undefined -Wl,-rpath-link=$(SYSLIBDIR) -nostdlib $(SYSLIBDIR)/crtbegin_dynamic.o $(SYSLIBDIR)/crtend_android.o -L$(SYSLIBDIR) -lc -ldl

all:    piranha

piranha:    piranha.c bstrlib.c bstrlib.h
	$(CC) $(CFLAGS) $(LDFLAGS) -Wall -o piranha piranha.c bstrlib.c

.PHONY: clean

clean:
	rm -f piranha

