SUBDIRS = compat

if CREATE_RULES_SYMLINK
#  extra_rules_file = $(xkb_rules_symlink)
#  extra_dir_file   = $(extra_rules_file).xml 
#  extra_lst_file   = $(extra_rules_file).lst

install-data-hook:
	for file in `echo "$(xkb_rules_symlink)" | sed 's/,/ /'` ; do \
		extra_rules_file=$$file ; \
		extra_dir_file=$$extra_rules_file.xml ; \
		extra_lst_file=$$extra_rules_file.lst ; \
		ln -sf base     $(DESTDIR)$(rulesdir)/$$extra_rules_file ; \
		ln -sf base.lst $(DESTDIR)$(rulesdir)/$$extra_lst_file ; \
		ln -sf base.xml $(DESTDIR)$(rulesdir)/$$extra_dir_file ; \
	done

else
install-data-hook:

endif

if USE_COMPAT_RULES

parts = \
	base.hdr.part base.lists.part \
	RULESET.lists.base.part \
	compat/base.lists.part \
	RULESET.m_k.part \
	base.l1_k.part \
	base.l_k.part \
	base.o_k.part \
	base.ml_g.part \
	base.m_g.part \
	compat/base.mlv_s.part   base.mlv_s.part \
	compat/base.ml_s.part    base.ml_s.part  \
	compat/base.ml1_s.part   base.ml1_s.part \
	compat/base.ml1v1_s.part \
	compat/base.ml2_s.part    base.ml2_s.part  \
	compat/base.ml3_s.part    base.ml3_s.part  \
	compat/base.ml4_s.part    base.ml4_s.part  \
	compat/base.ml2v2_s.part  \
	compat/base.ml3v3_s.part  \
	compat/base.ml4v4_s.part  \
	RULESET.m_s.part \
	RULESET.ml_s1.part \
	compat/base.lv_c.part   \
	compat/base.l1v1_c.part \
	compat/base.l2v2_c.part \
	compat/base.l3v3_c.part \
	compat/base.l4v4_c.part \
	base.ml_c.part \
	base.ml1_c.part \
	base.m_t.part \
	base.lo_s.part \
	base.l1o_s.part \
	base.l2o_s.part \
	base.l3o_s.part \
	base.l4o_s.part \
	compat/base.o_s.part     base.o_s.part \
	base.o_c.part \
	base.o_t.part

else

parts = \
	base.hdr.part base.lists.part \
	RULESET.lists.base.part \
	RULESET.m_k.part \
	base.l1_k.part \
	base.l_k.part \
	base.ml_g.part \
	base.m_g.part \
	base.mlv_s.part \
	base.ml_s.part  \
	base.ml1_s.part \
	base.ml2_s.part  \
	base.ml3_s.part  \
	base.ml4_s.part  \
	RULESET.m_s.part \
	RULESET.ml_s1.part \
	base.ml_c.part \
	base.ml1_c.part \
	base.m_t.part \
	base.l1o_s.part \
	base.l2o_s.part \
	base.l3o_s.part \
	base.l4o_s.part \
	base.o_s.part \
	base.o_c.part \
	base.o_t.part

endif

all_parts_dist = base.hdr.part \
	base.lists.part \
	base.lists.base.part \
	evdev.lists.base.part \
	base.m_k.part \
	base.l1_k.part \
	base.l_k.part \
	base.o_k.part \
	base.ml_g.part \
	base.m_g.part \
	base.mlv_s.part \
	base.ml_s.part \
	base.ml1_s.part \
	base.ml2_s.part \
	base.ml3_s.part \
	base.ml4_s.part \
	base.m_s.part \
	base.ml_s1.part \
	evdev.ml_s1.part \
	base.ml_c.part \
	base.ml1_c.part \
	base.m_t.part \
	base.lo_s.part \
	base.l1o_s.part \
	base.l2o_s.part \
	base.l3o_s.part \
	base.l4o_s.part \
	base.o_s.part \
	base.o_c.part \
	base.o_t.part \
	evdev.m_k.part \
	evdev.m_s.part

rules_files = base evdev xfree98

lst_files = base.lst evdev.lst

SUFFIXES = .xml .lst

.xml.lst:
	perl $(srcdir)/xml2lst.pl < $< > $@

base_parts=$(subst RULESET,base,$(parts))
base: $(base_parts) merge.py
	$(srcdir)/merge.py $@ $(srcdir) $(builddir) $(base_parts)

evdev_parts=$(subst RULESET,evdev,$(parts))
evdev: $(evdev_sources) merge.py
	$(srcdir)/merge.py $@ $(srcdir) $(builddir) $(evdev_parts)

dist_files = xkb.dtd README

rules_DATA = $(rules_files) $(lst_files) $(dist_files)

xml_DATA = base.xml evdev.xml base.extras.xml evdev.extras.xml

$(srcdir)/evdev.xml: base.xml
	cp $(srcdir)/base.xml $@

$(srcdir)/evdev.extras.xml: base.extras.xml
	cp $(srcdir)/base.extras.xml $@

EXTRA_DIST= $(xml_DATA) xfree98 \
            $(all_parts_dist) \
            xml2lst.pl merge.py xkb.its $(dist_files)

CLEANFILES = base base.lst evdev evdev.lst

rulesdir = $(xkb_base)/rules
xmldir = $(rulesdir)
