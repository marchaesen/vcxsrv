INCLUDES = 					\
	@KDRIVE_INCS@				\
	@KDRIVE_CFLAGS@

AM_CFLAGS =	-DHAVE_DIX_CONFIG_H

noinst_LTLIBRARIES = libkdrive.la libkdrivestubs.la

if XV
KDRIVE_XV_SOURCES =	\
	kxv.c		\
	kxv.h
endif

libkdrive_la_SOURCES =	\
	fourcc.h	\
	kcmap.c		\
	kdrive.c	\
	kdrive.h	\
	kinfo.c		\
	kinput.c	\
	kmode.c		\
	kshadow.c	\
	$(KDRIVE_XV_SOURCES) \
        $(top_srcdir)/mi/miinitext.c

libkdrivestubs_la_SOURCES = \
        $(top_srcdir)/fb/fbcmap_mi.c
