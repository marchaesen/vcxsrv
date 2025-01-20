/*
 * Stub version of enable_dit(), included in applications like
 * PuTTYtel and pterm which completely leave out the 'crypto' source
 * directory.
 */

#include "ssh.h"

#if HAVE_ARM_DIT

void enable_dit(void)
{
}

#endif
