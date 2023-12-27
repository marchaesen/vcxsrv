#include <stdlib.h>
#include "check_suites.h"

#if CHECK_MAJOR_VERSION == 0 && CHECK_MINOR_VERSION < 13
void suite_add_test(Suite *s, TFun tf, const char *name)
#else
void suite_add_test(Suite *s, const TTest *tt, const char *name)
#endif
{
	TCase *tc = tcase_create(name);

#if CHECK_MAJOR_VERSION == 0 && CHECK_MINOR_VERSION < 13
	tcase_add_test(tc, tf);
#else
	tcase_add_test(tc, tt);
#endif
	suite_add_tcase(s, tc);
}

int main(void)
{
	int nf;
	SRunner *sr = srunner_create(public_suite());
	srunner_set_xml(sr, "CheckLog_xcb.xml");
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
