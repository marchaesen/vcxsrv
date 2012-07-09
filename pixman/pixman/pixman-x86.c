/*
 * Copyright © 2000 SuSE, Inc.
 * Copyright © 2007 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of SuSE not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  SuSE makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * SuSE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL SuSE
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pixman-private.h"

#if defined(USE_X86_MMX) || defined (USE_SSE2)

/* The CPU detection code needs to be in a file not compiled with
 * "-mmmx -msse", as gcc would generate CMOV instructions otherwise
 * that would lead to SIGILL instructions on old CPUs that don't have
 * it.
 */
#if !defined(__amd64__) && !defined(__x86_64__) && !defined(_M_AMD64)

#ifdef HAVE_GETISAX
#include <sys/auxv.h>
#endif

typedef enum
{
    NO_FEATURES = 0,
    MMX = 0x1,
    MMX_EXTENSIONS = 0x2,
    SSE = 0x6,
    SSE2 = 0x8,
    CMOV = 0x10
} cpu_features_t;


static unsigned int
detect_cpu_features (void)
{
    unsigned int features = 0;
    unsigned int result = 0;
    
#ifdef HAVE_GETISAX
    if (getisax (&result, 1))
    {
	if (result & AV_386_CMOV)
	    features |= CMOV;
	if (result & AV_386_MMX)
	    features |= MMX;
	if (result & AV_386_AMD_MMX)
	    features |= MMX_EXTENSIONS;
	if (result & AV_386_SSE)
	    features |= SSE;
	if (result & AV_386_SSE2)
	    features |= SSE2;
    }
#else
    char vendor[13];
#ifdef _MSC_VER
    int vendor0 = 0, vendor1, vendor2;
#endif
    vendor[0] = 0;
    vendor[12] = 0;
    
#ifdef __GNUC__
    /* see p. 118 of amd64 instruction set manual Vol3 */
    /* We need to be careful about the handling of %ebx and
     * %esp here. We can't declare either one as clobbered
     * since they are special registers (%ebx is the "PIC
     * register" holding an offset to global data, %esp the
     * stack pointer), so we need to make sure they have their
     * original values when we access the output operands.
     */
    __asm__ (
        "pushf\n"
        "pop %%eax\n"
        "mov %%eax, %%ecx\n"
        "xor $0x00200000, %%eax\n"
        "push %%eax\n"
        "popf\n"
        "pushf\n"
        "pop %%eax\n"
        "mov $0x0, %%edx\n"
        "xor %%ecx, %%eax\n"
        "jz 1f\n"
	
        "mov $0x00000000, %%eax\n"
        "push %%ebx\n"
        "cpuid\n"
        "mov %%ebx, %%eax\n"
        "pop %%ebx\n"
        "mov %%eax, %1\n"
        "mov %%edx, %2\n"
        "mov %%ecx, %3\n"
        "mov $0x00000001, %%eax\n"
        "push %%ebx\n"
        "cpuid\n"
        "pop %%ebx\n"
        "1:\n"
        "mov %%edx, %0\n"
	: "=r" (result),
	  "=m" (vendor[0]),
	  "=m" (vendor[4]),
	  "=m" (vendor[8])
	:
	: "%eax", "%ecx", "%edx"
        );
    
#elif defined (_MSC_VER)
    
    _asm {
	pushfd
	    pop eax
	    mov ecx, eax
	    xor eax, 00200000h
	    push eax
	    popfd
	    pushfd
	    pop eax
	    mov edx, 0
	    xor eax, ecx
	    jz nocpuid
	    
	    mov eax, 0
	    push ebx
	    cpuid
	    mov eax, ebx
	    pop ebx
	    mov vendor0, eax
	    mov vendor1, edx
	    mov vendor2, ecx
	    mov eax, 1
	    push ebx
	    cpuid
	    pop ebx
	    nocpuid:
	    mov result, edx
	    }
    memmove (vendor + 0, &vendor0, 4);
    memmove (vendor + 4, &vendor1, 4);
    memmove (vendor + 8, &vendor2, 4);
    
#else
#   error unsupported compiler
#endif
    
    features = 0;
    if (result)
    {
	/* result now contains the standard feature bits */
	if (result & (1 << 15))
	    features |= CMOV;
	if (result & (1 << 23))
	    features |= MMX;
	if (result & (1 << 25))
	    features |= SSE;
	if (result & (1 << 26))
	    features |= SSE2;
	if ((features & MMX) && !(features & SSE) &&
	    (strcmp (vendor, "AuthenticAMD") == 0 ||
	     strcmp (vendor, "Geode by NSC") == 0))
	{
	    /* check for AMD MMX extensions */
#ifdef __GNUC__
	    __asm__ (
	        "	push %%ebx\n"
	        "	mov $0x80000000, %%eax\n"
	        "	cpuid\n"
	        "	xor %%edx, %%edx\n"
	        "	cmp $0x1, %%eax\n"
	        "	jge 2f\n"
	        "	mov $0x80000001, %%eax\n"
	        "	cpuid\n"
	        "2:\n"
	        "	pop %%ebx\n"
	        "	mov %%edx, %0\n"
		: "=r" (result)
		:
		: "%eax", "%ecx", "%edx"
	        );
#elif defined _MSC_VER
	    _asm {
		push ebx
		    mov eax, 80000000h
		    cpuid
		    xor edx, edx
		    cmp eax, 1
		    jge notamd
		    mov eax, 80000001h
		    cpuid
		    notamd:
		    pop ebx
		    mov result, edx
		    }
#endif
	    if (result & (1 << 22))
		features |= MMX_EXTENSIONS;
	}
    }
#endif /* HAVE_GETISAX */
    
    return features;
}

#ifdef USE_X86_MMX
static pixman_bool_t
pixman_have_mmx (void)
{
    static pixman_bool_t initialized = FALSE;
    static pixman_bool_t mmx_present;
    
    if (!initialized)
    {
	unsigned int features = detect_cpu_features ();
	mmx_present = (features & (MMX | MMX_EXTENSIONS)) == (MMX | MMX_EXTENSIONS);
	initialized = TRUE;
    }
    
    return mmx_present;
}
#endif

#ifdef USE_SSE2
static pixman_bool_t
pixman_have_sse2 (void)
{
    static pixman_bool_t initialized = FALSE;
    static pixman_bool_t sse2_present;
    
    if (!initialized)
    {
	unsigned int features = detect_cpu_features ();
	sse2_present = (features & (MMX | MMX_EXTENSIONS | SSE | SSE2)) == (MMX | MMX_EXTENSIONS | SSE | SSE2);
	initialized = TRUE;
    }
    
    return sse2_present;
}

#endif

#else /* __amd64__ */
#ifdef USE_X86_MMX
#define pixman_have_mmx() TRUE
#endif
#ifdef USE_SSE2
#define pixman_have_sse2() TRUE
#endif
#endif /* __amd64__ */

#endif

pixman_implementation_t *
_pixman_x86_get_implementations (pixman_implementation_t *imp)
{
#ifdef USE_X86_MMX
    if (!_pixman_disabled ("mmx") && pixman_have_mmx())
	imp = _pixman_implementation_create_mmx (imp);
#endif

#ifdef USE_SSE2
    if (!_pixman_disabled ("sse2") && pixman_have_sse2())
	imp = _pixman_implementation_create_sse2 (imp);
#endif

    return imp;
}
