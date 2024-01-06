encodingsdir = @ENCODINGSDIR@/large

ENCODING_FILES =		\
	big5.eten-0.enc		\
	big5hkscs-0.enc		\
	cns11643-1.enc		\
	cns11643-2.enc		\
	cns11643-3.enc		\
	gb18030-0.enc		\
	gb18030.2000-0.enc	\
	gb18030.2000-1.enc	\
	gb2312.1980-0.enc	\
	gbk-0.enc		\
	jisx0201.1976-0.enc	\
	jisx0208.1990-0.enc	\
	jisx0212.1990-0.enc	\
	ksc5601.1987-0.enc	\
	ksc5601.1992-3.enc	\
	sun.unicode.india-0.enc

EXTRA_DIST = $(ENCODING_FILES)

COMPRESSED = $(ENCODING_FILES:%=%.gz)

CLEANFILES = encodings.dir

if GZIP_LARGE
DATA_FILES = $(COMPRESSED)
CLEANFILES += $(COMPRESSED)
else
DATA_FILES = $(ENCODING_FILES)
endif

SUFFIXES = .enc .enc.gz

.enc.enc.gz:
	$(AM_V_GEN)$(GZIP) $(COMPRESS_FLAGS) -c < $< > $@

encodings.dir: $(DATA_FILES)
	$(AM_V_GEN)$(MKFONTSCALE) -b -s -l -n -r -p $(encodingsdir) -e . .

encodings_DATA = $(DATA_FILES) encodings.dir
