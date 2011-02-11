DEPS=../../deps

SOURCES=driver.ml
LIBS=$(DEPS)/extlib/extLib unix
INCDIRS=$(DEPS)/extlib
RESULT=piranha-driver
PRE_TARGETS=extlib.stamp

OCAMLMAKEFILE=$(DEPS)/OCamlMakefile
include $(OCAMLMAKEFILE)

.PHONY: extlib.stamp

extlib.stamp:
	$(MAKE) -C $(DEPS)/extlib

clean::
	$(MAKE) -C $(DEPS)/extlib clean

