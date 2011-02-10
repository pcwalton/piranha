SOURCES=symbolicate.ml
LIBS=../deps/extlib/extLib ../deps/ocurl/curl unix
INCDIRS=../deps/extlib ../deps/ocurl
RESULT=piranha-symbolicate
PRE_TARGETS=extlib.stamp ocurl.stamp

OCAMLMAKEFILE=../deps/OCamlMakefile
include ../deps/OCamlMakefile

.PHONY: extlib.stamp ocurl.stamp

extlib.stamp:
	$(MAKE) -C ../deps/extlib

ocurl.stamp:	../deps/ocurl/Makefile
	$(MAKE) -C ../deps/ocurl

../deps/ocurl/Makefile:
	cd ../deps/ocurl && env -i ./configure

clean::
	$(MAKE) -C ../deps/extlib clean && $(MAKE) -C ../deps/ocurl clean

