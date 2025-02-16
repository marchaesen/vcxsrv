.text
.arch armv7a
.object_arch armv4
.fpu neon
.eabi_attribute 10, 0
.arm
.altmacro
#ifndef __ARM_EABI__
#error EABI is required (to be sure that calling conventions are compatible)
#endif
pld [r0]
vmovn.u16 d0, q0
