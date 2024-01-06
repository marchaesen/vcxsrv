#  Copyright 2005 Red Hat, Inc.
#
#  Permission to use, copy, modify, distribute, and sell this software
#  and its documentation for any purpose is hereby granted without
#  fee, provided that the above copyright notice appear in all copies
#  and that both that copyright notice and this permission notice
#  appear in supporting documentation, and that the name of Red Hat
#  not be used in advertising or publicity pertaining to distribution
#  of the software without specific, written prior permission.  Red
#  Hat makes no representations about the suitability of this software
#  for any purpose.  It is provided "as is" without express or implied
#  warranty.
#
#  RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
#  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
#  NO EVENT SHALL RED HAT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
#  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
#  OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
#  NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
#  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

BDF2UCS_FONT_FILES = \
	courB08 \
	courB10 \
	courB12 \
	courB14 \
	courB18 \
	courB24 \
	courBO08 \
	courBO10 \
	courBO12 \
	courBO14 \
	courBO18 \
	courBO24 \
	courO08 \
	courO10 \
	courO12 \
	courO14 \
	courO18 \
	courO24 \
	courR08 \
	courR10 \
	courR12 \
	courR14 \
	courR18 \
	courR24 \
	helvB08 \
	helvB10 \
	helvB12 \
	helvB14 \
	helvB18 \
	helvB24 \
	helvBO08 \
	helvBO10 \
	helvBO12 \
	helvBO14 \
	helvBO18 \
	helvBO24 \
	helvO08 \
	helvO10 \
	helvO12 \
	helvO14 \
	helvO18 \
	helvO24 \
	helvR08 \
	helvR10 \
	helvR12 \
	helvR14 \
	helvR18 \
	helvR24 \
	ncenB08 \
	ncenB10 \
	ncenB12 \
	ncenB14 \
	ncenB18 \
	ncenB24 \
	ncenBI08 \
	ncenBI10 \
	ncenBI12 \
	ncenBI14 \
	ncenBI18 \
	ncenBI24 \
	ncenI08 \
	ncenI10 \
	ncenI12 \
	ncenI14 \
	ncenI18 \
	ncenI24 \
	ncenR08 \
	ncenR10 \
	ncenR12 \
	ncenR14 \
	ncenR18 \
	ncenR24 \
	timB08 \
	timB10 \
	timB12 \
	timB14 \
	timB18 \
	timB24 \
	timBI08 \
	timBI10 \
	timBI12 \
	timBI14 \
	timBI18 \
	timBI24 \
	timI08 \
	timI10 \
	timI12 \
	timI14 \
	timI18 \
	timI24 \
	timR08 \
	timR10 \
	timR12 \
	timR14 \
	timR18 \
	timR24
EXTRA_FONT_FILES = \
	symb08 \
	symb10 \
	symb12 \
	symb14 \
	symb18 \
	symb24
FONT_FILES = $(BDF2UCS_FONT_FILES) $(EXTRA_FONT_FILES)

BDF_FILES = $(FONT_FILES:%=%.bdf)
PCF_FILES = $(FONT_FILES:%=%.pcf$(COMPRESS_SUFFIX))

if ISO8859_1
ISO8859_1_BDF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-1.bdf)
ISO8859_1_PCF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-1.pcf$(COMPRESS_SUFFIX))
endif

if ISO8859_2
ISO8859_2_BDF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-2.bdf)
ISO8859_2_PCF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-2.pcf$(COMPRESS_SUFFIX))
endif

if ISO8859_3
ISO8859_3_BDF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-3.bdf)
ISO8859_3_PCF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-3.pcf$(COMPRESS_SUFFIX))
endif

if ISO8859_4
ISO8859_4_BDF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-4.bdf)
ISO8859_4_PCF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-4.pcf$(COMPRESS_SUFFIX))
endif

if ISO8859_9
ISO8859_9_BDF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-9.bdf)
ISO8859_9_PCF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-9.pcf$(COMPRESS_SUFFIX))
endif

if ISO8859_10
ISO8859_10_BDF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-10.bdf)
ISO8859_10_PCF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-10.pcf$(COMPRESS_SUFFIX))
endif

if ISO8859_13
ISO8859_13_BDF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-13.bdf)
ISO8859_13_PCF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-13.pcf$(COMPRESS_SUFFIX))
endif

if ISO8859_14
ISO8859_14_BDF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-14.bdf)
ISO8859_14_PCF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-14.pcf$(COMPRESS_SUFFIX))
endif

if ISO8859_15
ISO8859_15_BDF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-15.bdf)
ISO8859_15_PCF_FILES = $(BDF2UCS_FONT_FILES:%=%-ISO8859-15.pcf$(COMPRESS_SUFFIX))
endif

UTIL_DIR = @MAPFILES_PATH@

SUFFIXES = .bdf .pcf$(COMPRESS_SUFFIX) \
	-ISO8859-1.bdf \
	-ISO8859-2.bdf \
	-ISO8859-3.bdf \
	-ISO8859-4.bdf \
	-ISO8859-9.bdf \
	-ISO8859-10.bdf \
	-ISO8859-13.bdf \
	-ISO8859-14.bdf \
	-ISO8859-15.bdf

.bdf.pcf$(COMPRESS_SUFFIX):
	@rm -f $@
	$(AM_V_GEN)$(BDFTOPCF) -t $< | $(COMPRESS) $(COMPRESS_FLAGS) > $@

.bdf-ISO8859-1.bdf:
	@rm -f $@
	$(AM_V_GEN)$(UCS2ANY) $< $(UTIL_DIR)/map-ISO8859-1 ISO8859-1

.bdf-ISO8859-2.bdf:
	@rm -f $@
	$(AM_V_GEN)$(UCS2ANY) $< $(UTIL_DIR)/map-ISO8859-2 ISO8859-2

.bdf-ISO8859-3.bdf:
	@rm -f $@
	$(AM_V_GEN)$(UCS2ANY) $< $(UTIL_DIR)/map-ISO8859-3 ISO8859-3

.bdf-ISO8859-4.bdf:
	@rm -f $@
	$(AM_V_GEN)$(UCS2ANY) $< $(UTIL_DIR)/map-ISO8859-4 ISO8859-4

.bdf-ISO8859-9.bdf:
	@rm -f $@
	$(AM_V_GEN)$(UCS2ANY) $< $(UTIL_DIR)/map-ISO8859-9 ISO8859-9

.bdf-ISO8859-10.bdf:
	@rm -f $@
	$(AM_V_GEN)$(UCS2ANY) $< $(UTIL_DIR)/map-ISO8859-10 ISO8859-10

.bdf-ISO8859-13.bdf:
	@rm -f $@
	$(AM_V_GEN)$(UCS2ANY) $< $(UTIL_DIR)/map-ISO8859-13 ISO8859-13

.bdf-ISO8859-14.bdf:
	@rm -f $@
	$(AM_V_GEN)$(UCS2ANY) $< $(UTIL_DIR)/map-ISO8859-14 ISO8859-14

.bdf-ISO8859-15.bdf:
	@rm -f $@
	$(AM_V_GEN)$(UCS2ANY) $< $(UTIL_DIR)/map-ISO8859-15 ISO8859-15

fontdir = @FONTDIR@
font_DATA = \
	$(PCF_FILES) \
	$(ISO8859_1_PCF_FILES) \
	$(ISO8859_2_PCF_FILES) \
	$(ISO8859_3_PCF_FILES) \
	$(ISO8859_4_PCF_FILES) \
	$(ISO8859_9_PCF_FILES) \
	$(ISO8859_10_PCF_FILES) \
	$(ISO8859_13_PCF_FILES) \
	$(ISO8859_14_PCF_FILES) \
	$(ISO8859_15_PCF_FILES)
BUILT_BDF_FILES = \
	$(ISO8859_1_BDF_FILES) \
	$(ISO8859_2_BDF_FILES) \
	$(ISO8859_3_BDF_FILES) \
	$(ISO8859_4_BDF_FILES) \
	$(ISO8859_9_BDF_FILES) \
	$(ISO8859_10_BDF_FILES) \
	$(ISO8859_13_BDF_FILES) \
	$(ISO8859_14_BDF_FILES) \
	$(ISO8859_15_BDF_FILES)
CLEANFILES = $(font_DATA) $(BUILT_BDF_FILES)

EXTRA_DIST = $(BDF_FILES) README.md

MAINTAINERCLEANFILES = ChangeLog INSTALL

all-local: $(BUILT_BDF_FILES) $(font_DATA)

install-data-hook:
	@rm -f $(DESTDIR)$(fontdir)/fonts.dir
	$(MKFONTDIR) $(DESTDIR)$(fontdir)
	@RUN_FCCACHE@

distuninstallcheck:
	@:


.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL
