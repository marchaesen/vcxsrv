SUBDIRS = digital_vndr fujitsu_vndr hp_vndr jolla_vndr macintosh_vndr nec_vndr nokia_vndr sharp_vndr sgi_vndr sony_vndr sun_vndr xfree68_vndr

ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

THISDIR=symbols

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\$(THISDIR)

dist_symbols_DATA = \
 am bd by ctrl epo gb id it kz lt mm no pl si tj us\
 ancient be ca cz es ge ie jp la lv mn np pt sk tm uz\
 apl bg capslock de et gh il ke latam ma mt nz ro sn tr vn\
 ara bqn cd.in dk eu gn in keypad latin mv olpc rs srvr_ctrl trans za\
 at br ch dz eurosign gr inet kg level2 md my parens ru sy tw\
 af au brai cm ee fi group iq kh level3 me nbsp pc rupeesign terminate typo\
 al az bt cn eg fo hr ir kpdl level5 mk ng ph se tg tz\
 altwin ba bw compose empty fr hu is kr lk ml nl pk shift th ua

DATA_FILES=$(dist_symbols_DATA:%=$(DESTDIR)\%)

load_makefile $(SUBDIRS:%=%\makefile MAKESERVER=0 DEBUG=$(DEBUG);)

extrastuff: $(SUBDIRS:%=%\all)

include ..\xkbrules.mak

$(DESTDIR)\cd: cd.in
	copy $< $@

