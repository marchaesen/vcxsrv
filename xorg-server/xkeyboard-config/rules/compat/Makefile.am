
transform_files=layoutsMapping.lst variantsMapping.lst

dist_parts=base.o_s.part \
base.lv_c.part \
base.l1v1_c.part \
base.l2v2_c.part \
base.l3v3_c.part \
base.l4v4_c.part

scripts_dir=$(srcdir)/../bin

scripts= \
base.ml2_s.part \
base.ml3_s.part \
base.ml4_s.part \
base.ml2v2_s.part \
base.ml3v3_s.part \
base.ml4v4_s.part \
base.ml_s.part \
base.ml1_s.part \
base.mlv_s.part \
base.ml1v1_s.part

if USE_COMPAT_RULES

noinst_SCRIPTS=$(scripts) $(dist_parts)

else

noinst_SCRIPTS=

endif

base.ml2_s.part: $(scripts_dir)/mln_s.sh $(transform_files)
	sh $(scripts_dir)/mln_s.sh 2 $(srcdir)

base.ml3_s.part: $(scripts_dir)/mln_s.sh $(transform_files)
	sh $(scripts_dir)/mln_s.sh 3 $(srcdir)

base.ml4_s.part: $(scripts_dir)/mln_s.sh $(transform_files)
	sh $(scripts_dir)/mln_s.sh 4 $(srcdir)

base.ml2v2_s.part: $(scripts_dir)/mlnvn_s.sh $(transform_files)
	sh $(scripts_dir)/mlnvn_s.sh 2 $(srcdir)

base.ml3v3_s.part: $(scripts_dir)/mlnvn_s.sh $(transform_files)
	sh $(scripts_dir)/mlnvn_s.sh 3 $(srcdir)

base.ml4v4_s.part: $(scripts_dir)/mlnvn_s.sh $(transform_files)
	sh $(scripts_dir)/mlnvn_s.sh 4 $(srcdir)

base.ml_s.part: $(scripts_dir)/ml_s.sh $(transform_files)
	sh $(scripts_dir)/ml_s.sh $(srcdir)

base.ml1_s.part: $(scripts_dir)/ml1_s.sh $(transform_files)
	sh $(scripts_dir)/ml1_s.sh $(srcdir)

base.mlv_s.part: $(scripts_dir)/mlv_s.sh $(transform_files)
	sh $(scripts_dir)/mlv_s.sh $(srcdir)

base.ml1v1_s.part: $(scripts_dir)/ml1v1_s.sh $(transform_files)
	sh $(scripts_dir)/ml1v1_s.sh $(srcdir)

EXTRA_DIST=base.lists.part $(transform_files) $(dist_parts)

CLEANFILES=$(scripts)
