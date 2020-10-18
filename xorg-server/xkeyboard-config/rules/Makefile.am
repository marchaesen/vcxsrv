rulesdir = $(xkb_base)/rules
xmldir = $(rulesdir)

EXTRA_DIST =
CLEANFILES =
BUILT_SOURCES =

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

# rules/compat parts
# Because we rely on the generated sources of that file, we need to keep these
# in the same Makefile.am

layout_mappings=$(srcdir)/compat/layoutsMapping.lst
variant_mappings=$(srcdir)/compat/variantsMapping.lst

compat_dist_parts = \
    compat/0003-lists.part \
    compat/0028-lv_c.part   \
    compat/0029-l1v1_c.part \
    compat/0030-l2v2_c.part \
    compat/0031-l3v3_c.part \
    compat/0032-l4v4_c.part \
    compat/0041-o_s.part \
    compat/0041-o_s.part \
    compat/0028-lv_c.part \
    compat/0029-l1v1_c.part \
    compat/0030-l2v2_c.part \
    compat/0031-l3v3_c.part \
    compat/0032-l4v4_c.part \
    $(NULL)

compat_generated_parts = \
    compat/0012-ml_s.part \
    compat/0014-ml1_s.part \
    compat/0017-ml2_s.part \
    compat/0019-ml3_s.part \
    compat/0021-ml4_s.part \
    compat/0010-mlv_s.part \
    compat/0016-ml1v1_s.part \
    compat/0023-ml2v2_s.part \
    compat/0024-ml3v3_s.part \
    compat/0025-ml4v4_s.part \
    $(NULL)

compat_parts = $(compat_generated_parts) $(compat_dist_parts)
if USE_COMPAT_RULES
BUILT_SOURCES += $(compat_parts)
endif

map_script = $(srcdir)/compat/map-variants.py

compat/0014-ml1_s.part: $(map_script) $(layout_mappings) $(variant_mappings)
	$(map_script) --want=mls --number=1 $@ $(layout_mappings) $(variant_mappings)

compat/0017-ml2_s.part: $(map_script) $(layout_mappings) $(variant_mappings)
	$(map_script) --want=mls --number=2 $@ $(layout_mappings) $(variant_mappings)

compat/0019-ml3_s.part: $(map_script) $(layout_mappings) $(variant_mappings)
	$(map_script) --want=mls --number=3 $@ $(layout_mappings) $(variant_mappings)

compat/0021-ml4_s.part: $(map_script) $(layout_mappings) $(variant_mappings)
	$(map_script) --want=mls --number=4 $@ $(layout_mappings) $(variant_mappings)

compat/0016-ml1v1_s.part: $(map_script) $(variant_mappings)
	$(map_script) --want=mlvs --number=1 $@ $(variant_mappings)

compat/0023-ml2v2_s.part: $(map_script) $(variant_mappings)
	$(map_script) --want=mlvs --number=2 $@ $(variant_mappings)

compat/0024-ml3v3_s.part: $(map_script) $(variant_mappings)
	$(map_script) --want=mlvs --number=3 $@ $(variant_mappings)

compat/0025-ml4v4_s.part: $(map_script) $(variant_mappings)
	$(map_script) --want=mlvs --number=4 $@ $(variant_mappings)

compat/0012-ml_s.part: $(map_script) $(layout_mappings) $(variant_mappings)
	$(map_script) --want=mls $@ $(layout_mappings) $(variant_mappings)

compat/0010-mlv_s.part: $(map_script) $(layout_mappings) $(variant_mappings)
	$(map_script) --want=mlvs $@ $(variant_mappings)

EXTRA_DIST += $(layout_mappings) $(variant_mappings) $(compat_dist_parts) $(map_script)
CLEANFILES += $(compat_generated_parts)

# end of the rules/compat parts, now for the parts in rules/

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

if USE_COMPAT_RULES
parts += $(compat_parts)
endif

rules_files = base evdev xfree98

lst_files = base.lst evdev.lst

SUFFIXES = .xml .lst

.xml.lst:
	perl $(srcdir)/xml2lst.pl < $< > $@

base_parts=$(subst RULESET,base,$(parts))
base: $(base_parts) merge.py
	$(srcdir)/merge.py --srcdir=$(srcdir) --builddir=$(builddir) $(base_parts) > $@

evdev_parts=$(subst RULESET,evdev,$(parts))
evdev: $(evdev_sources) merge.py
	$(srcdir)/merge.py --srcdir=$(srcdir) --builddir=$(builddir) $(evdev_parts) > $@

dist_files = xkb.dtd README

rules_DATA = $(rules_files) $(lst_files) $(dist_files)

xml_DATA = base.xml evdev.xml base.extras.xml evdev.extras.xml

$(srcdir)/evdev.xml: base.xml
	cp $(srcdir)/base.xml $@

$(srcdir)/evdev.extras.xml: base.extras.xml
	cp $(srcdir)/base.extras.xml $@

EXTRA_DIST += $(xml_DATA) xfree98 \
              $(base_parts) $(evdev_parts) $(compat_parts) \
              xml2lst.pl merge.py xkb.its $(dist_files)

CLEANFILES += base base.lst evdev evdev.lst
