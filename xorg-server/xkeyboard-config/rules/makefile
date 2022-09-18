ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

load_makefile compat\makefile MAKESERVER=0 DEBUG=$(DEBUG)

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\rules

USE_COMPAT_RULES = 1

parts = \
	0000-hdr.part \
	0001-lists.part \
	0002-RULESET.lists.part \
	0004-RULESET.m_k.part \
	0005-l1_k.part \
	0006-l_k.part \
	0007-o_k.part \
	0008-ml_g.part \
	0009-m_g.part \
	0011-mlv_s.part \
	0013-ml_s.part \
	0015-ml1_s.part \
	0018-ml2_s.part  \
	0020-ml3_s.part  \
	0022-ml4_s.part  \
	0026-RULESET.m_s.part \
	0027-RULESET.ml_s1.part \
	0033-ml_c.part \
	0034-ml1_c.part \
	0035-m_t.part \
	0036-lo_s.part \
	0037-l1o_s.part \
	0038-l2o_s.part \
	0039-l3o_s.part \
	0040-l4o_s.part \
	0042-o_s.part \
	0043-o_c.part \
	0044-o_t.part

compat_parts = \
	compat/0010-mlv_s.part \
	compat/0012-ml_s.part \
	compat/0014-ml1_s.part \
	compat/0016-ml1v1_s.part \
	compat/0017-ml2_s.part \
	compat/0019-ml3_s.part \
	compat/0021-ml4_s.part \
	compat/0023-ml2v2_s.part  \
	compat/0024-ml3v3_s.part  \
	compat/0025-ml4v4_s.part  \
	compat/0028-lv_c.part   \
	compat/0029-l1v1_c.part \
	compat/0030-l2v2_c.part \
	compat/0031-l3v3_c.part \
	compat/0032-l4v4_c.part \
	compat/0041-o_s.part

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
