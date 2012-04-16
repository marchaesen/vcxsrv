
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <X11/Xarch.h>

#ifndef FIXEDBASE
#define CHECKRETURN(b) if(width <= ((b) * 32)) return(base + (b))
#else
#define CHECKRETURN(b) if(width <= ((b) * 32)) return(base)
#endif

#if X_BYTE_ORDER == X_BIG_ENDIAN
#define SHIFT_L(value, shift) ((value) >> (shift))
#define SHIFT_R(value, shift) ((value) << (shift))
#else
#define SHIFT_L(value, shift) ((value) << (shift))
#define SHIFT_R(value, shift) ((value) >> (shift))
#endif

#ifndef MSBFIRST
#ifdef FIXEDBASE
#define WRITE_IN_BITORDER(dest, offset, data) *(dest) = data;
#else
#define WRITE_IN_BITORDER(dest, offset, data) *(dest + offset) = data;
#endif
#else
#ifdef FIXEDBASE
#define WRITE_IN_BITORDER(dest, offset, data) *(dest) = SWAP_BITS_IN_BYTES(data);
#else
#define WRITE_IN_BITORDER(dest, offset, data) *(dest + offset) = SWAP_BITS_IN_BYTES(data)
#endif
#endif

#ifdef FIXEDBASE
#ifdef MSBFIRST
#define WRITE_BITS(b)   *base = SWAP_BITS_IN_BYTES(b)
#define WRITE_BITS1(b) { \
	*base = byte_reversed_expand3[(b) & 0xFF] | \
		byte_reversed_expand3[((b) & 0xFF00) >> 8] << 24; }
#define WRITE_BITS2(b) { \
	*base = byte_reversed_expand3[(b) & 0xFF] | \
		byte_reversed_expand3[((b) & 0xFF00) >> 8] << 24; \
	*base = byte_reversed_expand3[((b) & 0xFF00) >> 8] >> 8 | \
		byte_reversed_expand3[((b) & 0xFF0000) >> 16] << 16; }
#define WRITE_BITS3(b) { \
	*base = byte_reversed_expand3[(b) & 0xFF] | \
		byte_reversed_expand3[((b) & 0xFF00) >> 8] << 24; \
	*base = byte_reversed_expand3[((b) & 0xFF00) >> 8] >> 8 | \
		byte_reversed_expand3[((b) & 0xFF0000) >> 16] << 16; \
	*base = byte_reversed_expand3[((b) & 0xFF0000) >> 16] >> 16 | \
		byte_reversed_expand3[((b) & 0xFF000000) >> 24] << 8; }
#else
#define WRITE_BITS(b)   *base = (b)
#define WRITE_BITS1(b) { \
	*base = byte_expand3[(b) & 0xFF] | \
		byte_expand3[((b) & 0xFF00) >> 8] << 24; }
#define WRITE_BITS2(b) { \
	*base = byte_expand3[(b) & 0xFF] | \
		byte_expand3[((b) & 0xFF00) >> 8] << 24; \
	*base = byte_expand3[((b) & 0xFF00) >> 8] >> 8 | \
		byte_expand3[((b) & 0xFF0000) >> 16] << 16; }
#define WRITE_BITS3(b) { \
	*base = byte_expand3[(b) & 0xFF] | \
		byte_expand3[((b) & 0xFF00) >> 8] << 24; \
	*base = byte_expand3[((b) & 0xFF00) >> 8] >> 8 | \
		byte_expand3[((b) & 0xFF0000) >> 16] << 16; \
	*base = byte_expand3[((b) & 0xFF0000) >> 16] >> 16 | \
		byte_expand3[((b) & 0xFF000000) >> 24] << 8; }
#endif
#else
#ifdef MSBFIRST
#define WRITE_BITS(b)   *(base++) = SWAP_BITS_IN_BYTES(b)
#define WRITE_BITS1(b) { \
	*(base++) = byte_reversed_expand3[(b) & 0xFF] | \
		byte_reversed_expand3[((b) & 0xFF00) >> 8] << 24; }
#define WRITE_BITS2(b) { \
	*(base) = byte_reversed_expand3[(b) & 0xFF] | \
		byte_reversed_expand3[((b) & 0xFF00) >> 8] << 24; \
	*(base + 1) = byte_reversed_expand3[((b) & 0xFF00) >> 8] >> 8 | \
		byte_reversed_expand3[((b) & 0xFF0000) >> 16] << 16; \
	base += 2; }
#define WRITE_BITS3(b) { \
	*(base) = byte_reversed_expand3[(b) & 0xFF] | \
		byte_reversed_expand3[((b) & 0xFF00) >> 8] << 24; \
	*(base + 1) = byte_reversed_expand3[((b) & 0xFF00) >> 8] >> 8 | \
		byte_reversed_expand3[((b) & 0xFF0000) >> 16] << 16; \
	*(base + 2) = byte_reversed_expand3[((b) & 0xFF0000) >> 16] >> 16 | \
		byte_reversed_expand3[((b) & 0xFF000000) >> 24] << 8; \
	base += 3; }
#else
#define WRITE_BITS(b)   *(base++) = (b)
#define WRITE_BITS1(b) { \
	*(base++) = byte_expand3[(b) & 0xFF] | \
		byte_expand3[((b) & 0xFF00) >> 8] << 24; }
#define WRITE_BITS2(b) { \
	*(base) = byte_expand3[(b) & 0xFF] | \
		byte_expand3[((b) & 0xFF00) >> 8] << 24; \
	*(base + 1) = byte_expand3[((b) & 0xFF00) >> 8] >> 8 | \
		byte_expand3[((b) & 0xFF0000) >> 16] << 16; \
	base += 2; }
#define WRITE_BITS3(b) { \
	*(base) = byte_expand3[(b) & 0xFF] | \
		byte_expand3[((b) & 0xFF00) >> 8] << 24; \
	*(base + 1) = byte_expand3[((b) & 0xFF00) >> 8] >> 8 | \
		byte_expand3[((b) & 0xFF0000) >> 16] << 16; \
	*(base + 2) = byte_expand3[((b) & 0xFF0000) >> 16] >> 16 | \
		byte_expand3[((b) & 0xFF000000) >> 24] << 8; \
	base += 3; }
#endif
#endif

#ifdef FIXEDBASE
#ifdef MSBFIRST
#define EXPNAME(x) x##MSBFirstFixedBase
#else
#define EXPNAME(x) x##LSBFirstFixedBase
#endif
#else
#ifdef MSBFIRST
#define EXPNAME(x) x##MSBFirst
#else
#define EXPNAME(x) x##LSBFirst
#endif
#endif
