transform_files=layoutsMapping.lst variantsMapping.lst

scripts_dir=..\bin

SCRIPTS= \
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

# Here we make a distinction when compiling from cygwin or from cmd
ifdef SHLVL
SH=sh
SHEXT=.sh
else
SH=cmd /c
SHEXT=.bat
endif

base.ml2_s.part: $(scripts_dir)\mln_s$(SHEXT) $(transform_files)
	$(SH) $< 2 .

base.ml3_s.part: $(scripts_dir)\mln_s$(SHEXT) $(transform_files)
	$(SH) $< 3 .

base.ml4_s.part: $(scripts_dir)\mln_s$(SHEXT) $(transform_files)
	$(SH) $< 4 .

base.ml2v2_s.part: $(scripts_dir)\mlnvn_s$(SHEXT) $(transform_files)
	$(SH) $< 2 .

base.ml3v3_s.part: $(scripts_dir)\mlnvn_s$(SHEXT) $(transform_files)
	$(SH) $< 3 .

base.ml4v4_s.part: $(scripts_dir)\mlnvn_s$(SHEXT) $(transform_files)
	$(SH) $< 4 .

base.ml_s.part: $(scripts_dir)\ml_s$(SHEXT) $(transform_files)
	$(SH) $< .

base.ml1_s.part: $(scripts_dir)\ml1_s$(SHEXT) $(transform_files)
	$(SH) $< .

base.mlv_s.part: $(scripts_dir)\mlv_s$(SHEXT) $(transform_files)
	$(SH) $< .

base.ml1v1_s.part: $(scripts_dir)\ml1v1_s$(SHEXT) $(transform_files)
	$(SH) $< .

all: $(SCRIPTS)
