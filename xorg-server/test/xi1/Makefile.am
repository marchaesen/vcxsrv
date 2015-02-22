if ENABLE_UNIT_TESTS
if HAVE_LD_WRAP
noinst_PROGRAMS =  \
	protocol-xchangedevicecontrol

TESTS=$(noinst_PROGRAMS)
TESTS_ENVIRONMENT = $(XORG_MALLOC_DEBUG_ENV)

AM_CFLAGS = $(DIX_CFLAGS) @XORG_CFLAGS@
AM_CPPFLAGS = @XORG_INCS@ -I$(srcdir)/../xi2
TEST_LDADD=../libxservertest.la $(XORG_SYS_LIBS) $(XSERVER_SYS_LIBS) $(GLX_SYS_LIBS)
COMMON_SOURCES=$(srcdir)/../xi2/protocol-common.c

if SPECIAL_DTRACE_OBJECTS
TEST_LDADD += $(OS_LIB) $(DIX_LIB)
endif

protocol_xchangedevicecontrol_LDADD=$(TEST_LDADD)

protocol_xchangedevicecontrol_LDFLAGS=$(AM_LDFLAGS) -Wl,-wrap,WriteToClient

protocol_xchangedevicecontrol_SOURCES=$(COMMON_SOURCES) protocol-xchangedevicecontrol.c

else
# Print that xi1-tests were skipped (exit code 77 for automake test harness)
TESTS = xi1-tests
CLEANFILES = $(TESTS)

xi1-tests:
	@echo 'echo "ld -wrap support required for xi1 unit tests, skipping"' > $@
	@echo 'exit 77' >> $@
	$(AM_V_GEN)chmod +x $@
endif
endif
