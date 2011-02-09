SOURCES=symbolicate.ml
LIBS=extlib/extLib unix
INCDIRS=extlib
PACKS=netclient
RESULT=piranha-symbolicate
PRE_TARGETS=extlib.stamp

include OCamlMakefile

.PHONY: extlib.stamp

extlib.stamp:
	$(MAKE) -C extlib

clean::
	$(MAKE) -C extlib clean

