#include <check.h>

#if CHECK_MAJOR_VERSION == 0 && CHECK_MINOR_VERSION < 13
void suite_add_test(Suite *s, TFun tf, const char *name);
#else
void suite_add_test(Suite *s, const TTest *tt, const char *name);
#endif
Suite *public_suite(void);
