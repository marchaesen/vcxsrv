check_SCRIPTS=run-test.sh
TESTS_ENVIRONMENT=src=${srcdir} EXEEXT=${EXEEXT} sh
TESTS=run-test.sh

TESTDATA=4x6.pcf 8x16.pcf out.expected fonts.conf.in

EXTRA_DIST=$(check_SCRIPTS) $(TESTDATA)

CLEANFILES=

-include $(top_srcdir)/git.mk
