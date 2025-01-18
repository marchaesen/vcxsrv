layout_mappings=layoutsMapping.lst
variant_mappings=variantsMapping.lst

scripts= \
    base.ml_s.part \
    base.ml1_s.part \
    base.ml2_s.part \
    base.ml3_s.part \
    base.ml4_s.part \
    base.mlv_s.part \
    base.ml1v1_s.part \
    base.ml2v2_s.part \
    base.ml3v3_s.part \
    base.ml4v4_s.part

base.ml_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(PYTHON3) map-variants.py --want=mls $@ $(layout_mappings) $(variant_mappings)

base.ml1_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(PYTHON3) map-variants.py --want=mls --number=1 $@ $(layout_mappings) $(variant_mappings)

base.ml2_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(PYTHON3) map-variants.py --want=mls --number=2 $@ $(layout_mappings) $(variant_mappings)

base.ml3_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(PYTHON3) map-variants.py --want=mls --number=3 $@ $(layout_mappings) $(variant_mappings)

base.ml4_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(PYTHON3) map-variants.py --want=mls --number=4 $@ $(layout_mappings) $(variant_mappings)

base.mlv_s.part: map-variants.py $(layout_mappings) $(variant_mappings)
	$(PYTHON3) map-variants.py --want=mlvs $@ $(layout_mappings) $(variant_mappings)

base.ml1v1_s.part: map-variants.py $(variant_mappings)
	$(PYTHON3) map-variants.py --want=mlvs --number=1 $@ $(variant_mappings)

base.ml2v2_s.part: map-variants.py $(variant_mappings)
	$(PYTHON3) map-variants.py --want=mlvs --number=2 $@ $(variant_mappings)

base.ml3v3_s.part: map-variants.py $(variant_mappings)
	$(PYTHON3) map-variants.py --want=mlvs --number=3 $@ $(variant_mappings)

base.ml4v4_s.part: map-variants.py $(variant_mappings)
	$(PYTHON3) map-variants.py --want=mlvs --number=4 $@ $(variant_mappings)

all: $(scripts)
