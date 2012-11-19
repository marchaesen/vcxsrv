#include <stdlib.h>
#include "check_suites.h"

void suite_add_test(Suite *s, TFun tf, const char *name)
{
	TCase *tc = tcase_create(name);
	tcase_add_test(tc, tf);
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
