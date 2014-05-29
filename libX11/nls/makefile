X11_LOCALEDATADIR = ..\..\xorg-server\locale

x11localedir = $(X11_LOCALEDATADIR)
x11locale_DATA = locale.alias locale.dir compose.dir

# Copied from ../configure.ac
locales = \
 am_ET.UTF-8  armscii-8   C           el_GR.UTF-8  en_US.UTF-8  fi_FI.UTF-8 \
 georgian-academy         georgian-ps ibm-cp1133   iscii-dev    isiri-3342 \
 iso8859-1    iso8859-10  iso8859-11  iso8859-13   iso8859-14   iso8859-15 \
 iso8859-2    iso8859-3   iso8859-4   iso8859-5    iso8859-6    iso8859-7 \
 iso8859-8    iso8859-9   iso8859-9e  ja           ja.JIS       ja_JP.UTF-8\
 ja.SJIS      ko          koi8-c      koi8-r                                  \
 koi8-u       ko_KR.UTF-8 microsoft-cp1251         microsoft-cp1255 \
 microsoft-cp1256         mulelao-1   nokhchi-1    pt_BR.UTF-8  ru_RU.UTF-8 \
 tatar-cyr    th_TH       th_TH.UTF-8 tscii-0      vi_VN.tcvn   vi_VN.viscii \
 zh_CN        zh_CN.gb18030           zh_CN.gbk    zh_CN.UTF-8  zh_HK.big5 \
 zh_HK.big5hkscs          zh_HK.UTF-8 zh_TW        zh_TW.big5   zh_TW.UTF-8

include ../cpprules.mak


$(X11_LOCALEDATADIR)\%: %.pre
	cl /nologo /EP $< -DXCOMM\#\# > $<.l1
	sed -e "/^[^\#][^	 ]*:/s/://" -e "/^[^\#].*[	 ].*:/d" < $<.l1 > $<.l2
	type $<.l2 $<.l1 > $@
	del $<.l1
	del $<.l2

# Per-locale data files

nobase_dist_x11locale_DATA = $(locales:%=%\XI18N_OBJS)

nobase_x11locale_DATA = $(locales:%=%\XLC_LOCALE) $(locales:%=%\Compose)

all_DATA = $(nobase_dist_x11locale_DATA) $(nobase_x11locale_DATA) $(x11locale_DATA)

all: $(all_DATA:%=$(X11_LOCALEDATADIR)\%)
