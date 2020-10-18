layout_mappings=$(srcdir)/layoutsMapping.lst
variant_mappings=$(srcdir)/variantsMapping.lst

dist_parts= \
    0041-o_s.part \
    0028-lv_c.part \
    0029-l1v1_c.part \
    0030-l2v2_c.part \
    0031-l3v3_c.part \
    0032-l4v4_c.part

scripts= \
    0012-ml_s.part \
    0014-ml1_s.part \
    0017-ml2_s.part \
    0019-ml3_s.part \
    0021-ml4_s.part \
    0010-mlv_s.part \
    0016-ml1v1_s.part \
    0023-ml2v2_s.part \
    0024-ml3v3_s.part \
    0025-ml4v4_s.part \
    $(NULL)

BUILT_SOURCES =

if USE_COMPAT_RULES
BUILT_SOURCES += $(scripts) $(dist_parts)
endif

0014-ml1_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(srcdir)/map-variants.py --want=mls --number=1 $@ $(layout_mappings) $(variant_mappings)

0017-ml2_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(srcdir)/map-variants.py --want=mls --number=2 $@ $(layout_mappings) $(variant_mappings)

0019-ml3_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(srcdir)/map-variants.py --want=mls --number=3 $@ $(layout_mappings) $(variant_mappings)

0021-ml4_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(srcdir)/map-variants.py --want=mls --number=4 $@ $(layout_mappings) $(variant_mappings)

0016-ml1v1_s.part: map-variants.py $(variant_mappings)
	$(srcdir)/map-variants.py --want=mlvs --number=1 $@ $(variant_mappings)

0023-ml2v2_s.part: map-variants.py $(variant_mappings)
	$(srcdir)/map-variants.py --want=mlvs --number=2 $@ $(variant_mappings)

0024-ml3v3_s.part: map-variants.py $(variant_mappings)
	$(srcdir)/map-variants.py --want=mlvs --number=3 $@ $(variant_mappings)

0025-ml4v4_s.part: map-variants.py $(variant_mappings)
	$(srcdir)/map-variants.py --want=mlvs --number=4 $@ $(variant_mappings)

0012-ml_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(srcdir)/map-variants.py --want=mls $@ $(layout_mappings) $(variant_mappings)

0010-mlv_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(srcdir)/map-variants.py --want=mlvs $@ $(variant_mappings)

EXTRA_DIST=0003-lists.part $(layout_mappings) $(variant_mappings) $(dist_parts) map-variants.py

CLEANFILES=$(scripts)
