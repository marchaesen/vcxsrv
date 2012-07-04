noinst_LTLIBRARIES = libi2c.la

multimediadir = $(moduledir)/multimedia
multimedia_LTLIBRARIES =	\
	bt829_drv.la		\
	fi1236_drv.la		\
	msp3430_drv.la		\
	tda8425_drv.la		\
	tda9850_drv.la		\
	tda9885_drv.la		\
	uda1380_drv.la

libi2c_la_SOURCES = xf86i2c.c

INCLUDES = $(XORG_INCS)

AM_CFLAGS = $(DIX_CFLAGS) $(XORG_CFLAGS)

sdk_HEADERS = xf86i2c.h bt829.h fi1236.h msp3430.h tda8425.h tda9850.h tda9885.h uda1380.h i2c_def.h

#
# i2c drivers
#
bt829_drv_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
bt829_drv_la_SOURCES = bt829.c bt829.h bt829_module.c

fi1236_drv_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
fi1236_drv_la_SOURCES = fi1236.c fi1236.h fi1236_module.c tda9885.c

msp3430_drv_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
msp3430_drv_la_SOURCES = msp3430.c msp3430.h msp3430_module.c

tda8425_drv_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
tda8425_drv_la_SOURCES = tda8425.c tda8425.h tda8425_module.c

tda9850_drv_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
tda9850_drv_la_SOURCES = tda9850.c tda9850.h tda9850_module.c

tda9885_drv_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
tda9885_drv_la_SOURCES = tda9885.c tda9885.h tda9885_module.c

uda1380_drv_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
uda1380_drv_la_SOURCES = uda1380.c uda1380.h uda1380_module.c
