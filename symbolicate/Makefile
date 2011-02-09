SOURCES=symbolicate.ml
LIBS=extlib/extLib ocurl/curl unix
INCDIRS=extlib ocurl
RESULT=piranha-symbolicate
PRE_TARGETS=extlib.stamp ocurl.stamp

include OCamlMakefile

.PHONY: extlib.stamp ocurl.stamp

extlib.stamp:
	$(MAKE) -C extlib

ocurl.stamp:	ocurl/Makefile
	$(MAKE) -C ocurl

ocurl/Makefile:
	cd ocurl && env -i ./configure

clean::
	$(MAKE) -C extlib clean && $(MAKE) -C ocurl clean

