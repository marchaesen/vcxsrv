if ENABLE_UNIT_TESTS
if HAVE_LD_WRAP
noinst_PROGRAMS =  \
	protocol-xiqueryversion \
	protocol-xiquerydevice \
	protocol-xiselectevents \
	protocol-xigetselectedevents \
        protocol-xisetclientpointer \
        protocol-xigetclientpointer \
        protocol-xipassivegrabdevice \
        protocol-xiquerypointer \
        protocol-xiwarppointer \
        protocol-eventconvert \
        xi2

TESTS=$(noinst_PROGRAMS)
TESTS_ENVIRONMENT = $(XORG_MALLOC_DEBUG_ENV)

AM_CFLAGS = $(DIX_CFLAGS) @XORG_CFLAGS@
AM_CPPFLAGS = @XORG_INCS@
TEST_LDADD=../libxservertest.la $(XORG_SYS_LIBS) $(XSERVER_SYS_LIBS) $(GLX_SYS_LIBS)
COMMON_SOURCES=protocol-common.h protocol-common.c

if SPECIAL_DTRACE_OBJECTS
TEST_LDADD += $(OS_LIB) $(DIX_LIB)
endif

protocol_xiqueryversion_LDADD=$(TEST_LDADD)
protocol_xiquerydevice_LDADD=$(TEST_LDADD)
protocol_xiselectevents_LDADD=$(TEST_LDADD)
protocol_xigetselectedevents_LDADD=$(TEST_LDADD)
protocol_xisetclientpointer_LDADD=$(TEST_LDADD)
protocol_xigetclientpointer_LDADD=$(TEST_LDADD)
protocol_xiquerypointer_LDADD=$(TEST_LDADD)
protocol_xipassivegrabdevice_LDADD=$(TEST_LDADD)
protocol_xiwarppointer_LDADD=$(TEST_LDADD)
protocol_eventconvert_LDADD=$(TEST_LDADD)
xi2_LDADD=$(TEST_LDADD)

protocol_xiqueryversion_LDFLAGS=$(AM_LDFLAGS) -Wl,-wrap,WriteToClient
protocol_xiquerydevice_LDFLAGS=$(AM_LDFLAGS) -Wl,-wrap,WriteToClient
protocol_xiselectevents_LDFLAGS=$(AM_LDFLAGS) -Wl,-wrap,dixLookupWindow -Wl,-wrap,XISetEventMask
protocol_xigetselectedevents_LDFLAGS=$(AM_LDFLAGS) -Wl,-wrap,WriteToClient -Wl,-wrap,dixLookupWindow -Wl,-wrap,AddResource
protocol_xisetclientpointer_LDFLAGS=$(AM_LDFLAGS) -Wl,-wrap,dixLookupClient
protocol_xigetclientpointer_LDFLAGS=$(AM_LDFLAGS) -Wl,-wrap,WriteToClient -Wl,-wrap,dixLookupClient
protocol_xipassivegrabdevice_LDFLAGS=$(AM_LDFLAGS) -Wl,-wrap,GrabButton -Wl,-wrap,dixLookupWindow -Wl,-wrap,WriteToClient
protocol_xiquerypointer_LDFLAGS=$(AM_LDFLAGS) -Wl,-wrap,WriteToClient -Wl,-wrap,dixLookupWindow
protocol_xiwarppointer_LDFLAGS=$(AM_LDFLAGS) -Wl,-wrap,WriteToClient -Wl,-wrap,dixLookupWindow
xi2_LDFLAGS=$(AM_LDFLAGS)

protocol_xiqueryversion_SOURCES=$(COMMON_SOURCES) protocol-xiqueryversion.c
protocol_xiquerydevice_SOURCES=$(COMMON_SOURCES) protocol-xiquerydevice.c
protocol_xiselectevents_SOURCES=$(COMMON_SOURCES) protocol-xiselectevents.c
protocol_xigetselectedevents_SOURCES=$(COMMON_SOURCES) protocol-xigetselectedevents.c
protocol_xisetclientpointer_SOURCES=$(COMMON_SOURCES) protocol-xisetclientpointer.c
protocol_xigetclientpointer_SOURCES=$(COMMON_SOURCES) protocol-xigetclientpointer.c
protocol_xiquerypointer_SOURCES=$(COMMON_SOURCES) protocol-xiquerypointer.c
protocol_xipassivegrabdevice_SOURCES=$(COMMON_SOURCES) protocol-xipassivegrabdevice.c
protocol_xiwarppointer_SOURCES=$(COMMON_SOURCES) protocol-xiwarppointer.c
else
# Print that xi2-tests were skipped (exit code 77 for automake test harness)
TESTS = xi2-tests
CLEANFILES = $(TESTS)

xi2-tests:
	@echo 'echo "ld -wrap support required for xi2 unit tests, skipping"' > $@
	@echo 'exit 77' >> $@
	$(AM_V_GEN)chmod +x $@
endif
endif
