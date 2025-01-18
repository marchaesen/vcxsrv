ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

load_makefile compat\makefile MAKESERVER=0 DEBUG=$(DEBUG)

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\rules

USE_COMPAT_RULES = 1

parts = \
  0000-hdr.part\
  0001-lists.part\
  0002-RULESET.lists.part\
  0004-RULESET.model_keycodes.part\
  0005-layout1_keycodes.part\
  0006-layout_keycodes.part\
  0007-options_keycodes.part\
  0008-modellayout_geometry.part\
  0009-model_geometry.part\
  0011-modellayoutvariant_symbols.part\
  0013-modellayout_symbols.part\
  0016-modellayout1_symbols.part\
  0018-modellayout2_symbols.part\
  0020-modellayout3_symbols.part\
  0022-modellayout4_symbols.part\
  0026-RULESET.model_symbols.part\
  0027-RULESET.modellayout_symbols1.part\
  0033-modellayout_compat.part\
  0034-modellayout1_compat.part\
  0035-model_types.part\
  0036-layoutoption_symbols.part\
  0037-layout1option_symbols.part\
  0038-layout2option_symbols.part\
  0039-layout3option_symbols.part\
  0040-layout4option_symbols.part\
  0012-ml_s.part\
  0015-ml1_s.part\
  0017-ml2_s.part\
  0019-ml3_s.part\
  0021-ml4_s.part\
  0010-mlv_s.part\
  0014-ml1v1_s.part\
  0023-ml2v2_s.part\
  0024-ml3v3_s.part\
  0025-ml4v4_s.part

compat_parts = \
  compat/0028-layoutvariant_compat.part\
  compat/0029-layout1variant1_compat.part\
  compat/0030-layout2variant2_compat.part\
  compat/0031-layout3variant3_compat.part\
  compat/0032-layout4variant4_compat.part\
  compat/0041-option_symbols.part

if USE_COMPAT_RULES
parts += $(compat_parts)
endif

rules_files = base evdev xfree98

lst_files = base.lst evdev.lst

$(DESTDIR)\%.lst: $(DESTDIR)\%.xml
	perl xml2lst.pl < $< > $@

base_parts=$(subst RULESET,base,$(parts))
$(DESTDIR)\base: $(base_parts) merge.py
	$(PYTHON3) merge.py --srcdir=. --builddir=$(DESTDIR) $(base_parts) > $@

evdev_parts=$(subst RULESET,evdev,$(parts))
$(DESTDIR)\evdev: $(evdev_parts) merge.py
	$(PYTHON3) merge.py --srcdir=. --builddir=$(DESTDIR) $(evdev_parts) > $@

dist_files = xkb.dtd README

rules_DATA = $(rules_files) $(lst_files) $(dist_files)

COMPATFILES=xorg xorg.xml xorg.lst
$(DESTDIR)\xorg: $(DESTDIR)\base
	copy $< $@
$(DESTDIR)\xorg%: $(DESTDIR)\base%
	copy $< $@

rules_DATA := $(rules_DATA) $(COMPATFILES)

xml_DATA = base.xml evdev.xml base.extras.xml evdev.extras.xml

$(DESTDIR)\evdev.xml: base.xml
	copy $< $@

$(DESTDIR)\evdev.extras.xml: base.extras.xml
	copy $< $@

DATA_FILES=$(rules_DATA:%=$(DESTDIR)\%) $(xml_DATA:%=$(DESTDIR)\%)

include ..\xkbrules.mak
