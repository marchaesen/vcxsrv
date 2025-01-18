/*
 * Enable the PSTATE.DIT flag in AArch64, if available.
 *
 * This guarantees that data-processing instructions (or rather, a
 * long list of specific ones) will have data-independent timing
 * (hence the name). In other words, you want to turn this bit on if
 * you're trying to do constant-time crypto.
 *
 * For maximum performance you'd want to turn this bit back off when
 * doing any CPU-intensive stuff that _isn't_ cryptographic. That
 * seems like a small concern in this code base, and carries the risk
 * of losing track of whether it was on or not, so here we just enable
 * it for the whole process. That's why there's only an enable_dit()
 * function in this file and not a disable_dit() to go with it.
 */

#include "ssh.h"

void enable_dit(void)
{
    if (!platform_dit_available())
        return;
    asm volatile("msr dit, %0" :: "r"(1));
}
