ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif
load_makefile large\makefile MAKESERVER=0 DEBUG=$(DEBUG)

INC_BDF_RULES=1

DESTDIR=$(MHMAKECONF)\xorg-server\fonts\encodings

ENCODING_FILES =		\
	adobe-dingbats.enc	\
	adobe-standard.enc	\
	adobe-symbol.enc	\
	armscii-8.enc		\
	ascii-0.enc		\
	dec-special.enc		\
	ibm-cp437.enc		\
	ibm-cp850.enc		\
	ibm-cp852.enc		\
	ibm-cp866.enc		\
	iso8859-11.enc		\
	iso8859-13.enc		\
	iso8859-16.enc		\
	iso8859-6.16.enc	\
	iso8859-6.8x.enc	\
	microsoft-cp1250.enc	\
	microsoft-cp1251.enc	\
	microsoft-cp1252.enc	\
	microsoft-cp1253.enc	\
	microsoft-cp1254.enc	\
	microsoft-cp1255.enc	\
	microsoft-cp1256.enc	\
	microsoft-cp1257.enc	\
	microsoft-cp1258.enc	\
	microsoft-win3.1.enc	\
	mulearabic-0.enc	\
	mulearabic-1.enc	\
	mulearabic-2.enc	\
	mulelao-1.enc		\
	suneu-greek.enc		\
	tcvn-0.enc		\
	tis620-2.enc		\
	viscii1.1-1.enc

COMPRESSED = $(ENCODING_FILES:%=$(DESTDIR)\%.gz)

DATA_FILES = $(COMPRESSED)

MKFONTSCALE=$(MHMAKECONF)\mkfontscale\$(OBJDIR)\mkfontscale.exe

load_makefile $(MHMAKECONF)\mkfontscale\makefile MAKESERVER=0 DEBUG=$(DEBUG)

$(DESTDIR)\encodings.dir: $(DATA_FILES) $(DESTDIR)\large\encodings.dir $(MKFONTSCALE)
	cd "$(DESTDIR)" & $(MKFONTSCALE) -b -s -l -n -r -p . -e . -e large .

all: $(DESTDIR)\encodings.dir

.PHONY: cleanthis
cleanthis:
	del -e $(DESTDIR)

clean: cleanthis

