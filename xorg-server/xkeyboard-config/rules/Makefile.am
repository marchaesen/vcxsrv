SUBDIRS = bin compat

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

base_parts = base.hdr.part base.lists.part \
base.lists.base.part \
compat/base.lists.part \
HDR base.m_k.part \
HDR base.l1_k.part \
HDR base.l_k.part \
HDR base.o_k.part \
HDR base.ml_g.part \
HDR base.m_g.part \
HDR compat/base.mlv_s.part   base.mlv_s.part \
HDR compat/base.ml_s.part    base.ml_s.part  \
HDR compat/base.ml1_s.part   base.ml1_s.part \
HDR compat/base.ml1v1_s.part \
HDR compat/base.ml2_s.part    base.ml2_s.part  \
HDR compat/base.ml3_s.part    base.ml3_s.part  \
HDR compat/base.ml4_s.part    base.ml4_s.part  \
HDR compat/base.ml2v2_s.part  \
HDR compat/base.ml3v3_s.part  \
HDR compat/base.ml4v4_s.part  \
HDR base.m_s.part \
HDR base.ml_s1.part \
HDR compat/base.lv_c.part   \
HDR compat/base.l1v1_c.part \
HDR compat/base.l2v2_c.part \
HDR compat/base.l3v3_c.part \
HDR compat/base.l4v4_c.part \
HDR base.ml_c.part \
HDR base.ml1_c.part \
HDR base.m_t.part \
HDR base.lo_s.part \
HDR base.l1o_s.part \
HDR base.l2o_s.part \
HDR base.l3o_s.part \
HDR base.l4o_s.part \
HDR compat/base.o_s.part     base.o_s.part \
HDR base.o_c.part \
HDR base.o_t.part

evdev_parts = base.hdr.part base.lists.part \
evdev.lists.part \
compat/base.lists.part \
HDR evdev.m_k.part \
HDR base.l1_k.part \
HDR base.l_k.part \
HDR base.o_k.part \
HDR base.ml_g.part \
HDR base.m_g.part \
HDR compat/base.mlv_s.part   base.mlv_s.part \
HDR compat/base.ml_s.part    base.ml_s.part  \
HDR compat/base.ml1_s.part   base.ml1_s.part \
HDR compat/base.ml1v1_s.part \
HDR compat/base.ml2_s.part    base.ml2_s.part  \
HDR compat/base.ml3_s.part    base.ml3_s.part  \
HDR compat/base.ml4_s.part    base.ml4_s.part  \
HDR compat/base.ml2v2_s.part  \
HDR compat/base.ml3v3_s.part  \
HDR compat/base.ml4v4_s.part  \
HDR evdev.m_s.part \
HDR \
HDR compat/base.lv_c.part   \
HDR compat/base.l1v1_c.part \
HDR compat/base.l2v2_c.part \
HDR compat/base.l3v3_c.part \
HDR compat/base.l4v4_c.part \
HDR base.ml_c.part \
HDR base.ml1_c.part \
HDR base.m_t.part \
HDR base.lo_s.part \
HDR base.l1o_s.part \
HDR base.l2o_s.part \
HDR base.l3o_s.part \
HDR base.l4o_s.part \
HDR compat/base.o_s.part     base.o_s.part \
HDR base.o_c.part \
HDR base.o_t.part

else

base_parts = base.hdr.part base.lists.part \
base.lists.base.part \
HDR base.m_k.part \
HDR base.l1_k.part \
HDR base.l_k.part \
HDR \
HDR base.ml_g.part \
HDR base.m_g.part \
HDR base.mlv_s.part \
HDR base.ml_s.part  \
HDR base.ml1_s.part \
HDR \
HDR base.ml2_s.part  \
HDR base.ml3_s.part  \
HDR base.ml4_s.part  \
HDR \
HDR \
HDR \
HDR base.m_s.part \
HDR base.ml_s1.part \
HDR \
HDR \
HDR \
HDR \
HDR \
HDR base.ml_c.part \
HDR base.ml1_c.part \
HDR base.m_t.part \
HDR \
HDR base.l1o_s.part \
HDR base.l2o_s.part \
HDR base.l3o_s.part \
HDR base.l4o_s.part \
HDR base.o_s.part \
HDR base.o_c.part \
HDR base.o_t.part

evdev_parts = base.hdr.part base.lists.part \
evdev.lists.part \
HDR evdev.m_k.part \
HDR base.l1_k.part \
HDR base.l_k.part  \
HDR \
HDR base.ml_g.part \
HDR base.m_g.part \
HDR base.mlv_s.part \
HDR base.ml_s.part  \
HDR base.ml1_s.part \
HDR \
HDR base.ml2_s.part  \
HDR base.ml3_s.part  \
HDR base.ml4_s.part  \
HDR \
HDR \
HDR \
HDR evdev.m_s.part \
HDR \
HDR \
HDR \
HDR \
HDR \
HDR \
HDR base.ml_c.part \
HDR base.ml1_c.part \
HDR base.m_t.part \
HDR \
HDR base.l1o_s.part \
HDR base.l2o_s.part \
HDR base.l3o_s.part \
HDR base.l4o_s.part \
HDR base.o_s.part \
HDR base.o_c.part \
HDR base.o_t.part

endif

all_parts_dist = HDR base.hdr.part \
base.lists.part \
base.lists.base.part \
evdev.lists.part \
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

%.lst: %.xml
	perl $(srcdir)/xml2lst.pl < $< > $@

base: $(base_parts)
	HDR="$(srcdir)/HDR" $(srcdir)/merge.sh $@ $(base_parts)

evdev: $(evdev_parts)
	HDR="$(srcdir)/HDR" $(srcdir)/merge.sh $@ $(evdev_parts)

dist_files = xkb.dtd README

rules_DATA = $(rules_files) $(lst_files) $(dist_files)

xml_in_files = base.xml.in evdev.xml.in base.extras.xml.in evdev.extras.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml) 

$(srcdir)/evdev.xml.in: base.xml.in
	cp $< $@

$(srcdir)/evdev.extras.xml.in: base.extras.xml.in
	cp $< $@

EXTRA_DIST= $(xml_in_files) xfree98 \
            $(all_parts_dist) \
            xml2lst.pl merge.sh $(dist_files)

CLEANFILES = base base.xml base.lst evdev evdev.xml evdev.lst base.extras.xml evdev.extras.xml
MAINTAINERCLEANFILES = $(srcdir)/evdev.xml.in $(srcdir)/evdev.extras.xml.in

rulesdir = $(xkb_base)/rules
xmldir = $(rulesdir)

@INTLTOOL_XML_NOMERGE_RULE@
