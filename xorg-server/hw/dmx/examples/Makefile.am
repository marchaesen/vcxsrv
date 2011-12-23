if DMX_BUILD_USB 
# Requires <linux/input.h>
EV_PROG = ev
endif

bin_PROGRAMS = \
	dmxinfo dmxwininfo dmxreconfig dmxresize \
	dmxaddscreen dmxrmscreen \
	dmxaddinput dmxrminput
noinst_PROGRAMS = xinput xtest evi res xled xbell $(EV_PROG)

dmxinfo_SOURCES = dmxinfo.c
dmxinfo_LDADD = $(DMXEXAMPLES_DEP_LIBS)
dmxinfo_CFLAGS = $(DMXEXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

dmxwininfo_SOURCES = dmxwininfo.c
dmxwininfo_LDADD = $(DMXXMUEXAMPLES_DEP_LIBS)
dmxwininfo_CFLAGS = $(DMXXMUEXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

dmxreconfig_SOURCES = dmxreconfig.c
dmxreconfig_LDADD = $(DMXEXAMPLES_DEP_LIBS)
dmxreconfig_CFLAGS = $(DMXEXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

dmxresize_SOURCES = dmxresize.c
dmxresize_LDADD = $(DMXEXAMPLES_DEP_LIBS)
dmxresize_CFLAGS = $(DMXEXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

dmxaddscreen_SOURCES = dmxaddscreen.c
dmxaddscreen_LDADD = $(DMXEXAMPLES_DEP_LIBS)
dmxaddscreen_CFLAGS = $(DMXEXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

dmxrmscreen_SOURCES = dmxrmscreen.c
dmxrmscreen_LDADD = $(DMXEXAMPLES_DEP_LIBS)
dmxrmscreen_CFLAGS = $(DMXEXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

dmxaddinput_SOURCES = dmxaddinput.c
dmxaddinput_LDADD = $(DMXEXAMPLES_DEP_LIBS)
dmxaddinput_CFLAGS = $(DMXEXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

dmxrminput_SOURCES = dmxrminput.c
dmxrminput_LDADD = $(DMXEXAMPLES_DEP_LIBS)
dmxrminput_CFLAGS = $(DMXEXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

xinput_SOURCES = xinput.c
xinput_LDADD = $(DMXXIEXAMPLES_DEP_LIBS)
xinput_CFLAGS = $(DMXXIEXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

xtest_SOURCES = xtest.c
xtest_LDADD = $(XTSTEXAMPLES_DEP_LIBS)
xtest_CFLAGS = $(XTSTEXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

evi_SOURCES = evi.c
evi_LDADD = $(X11EXAMPLES_DEP_LIBS)
evi_CFLAGS = $(X11EXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

res_SOURCES = res.c
res_LDADD = $(XRESEXAMPLES_DEP_LIBS)
res_CFLAGS = $(XRESEXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

xled_SOURCES = xled.c
xled_LDADD = $(X11EXAMPLES_DEP_LIBS)
xled_CFLAGS = $(X11EXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

xbell_SOURCES = xbell.c
xbell_LDADD = $(X11EXAMPLES_DEP_LIBS)
xbell_CFLAGS = $(X11EXAMPLES_DEP_CFLAGS) $(CWARNFLAGS)

ev_SOURCES = ev.c
ev_LDADD = 
ev_CFLAGS = 
