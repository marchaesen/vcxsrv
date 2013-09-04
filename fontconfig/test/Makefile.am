check_SCRIPTS=run-test.sh
TEST_EXTENSIONS = \
	.sh \
	$(NULL)

AM_TESTS_ENVIRONMENT= \
	src=${srcdir}; export src; \
	EXEEXT=${EXEEXT}; export EXEEXT; \
	$(NULL)

SH_LOG_COMPILER = sh

TESTS=run-test.sh

TESTDATA=4x6.pcf 8x16.pcf out.expected fonts.conf.in

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_builddir)

check_PROGRAMS = test-migration
if HAVE_PTHREAD
check_PROGRAMS += test-pthread
test_pthread_LDADD = $(top_builddir)/src/libfontconfig.la
# We don't enable this test by default because it will require config and fonts
# to meaningfully test anything, and we are not installed yet.
#TESTS += test-pthread
endif
noinst_PROGRAMS = $(check_PROGRAMS)

test_migration_LDADD = $(top_builddir)/src/libfontconfig.la

EXTRA_DIST=$(check_SCRIPTS) $(TESTDATA)

CLEANFILES=

-include $(top_srcdir)/git.mk
