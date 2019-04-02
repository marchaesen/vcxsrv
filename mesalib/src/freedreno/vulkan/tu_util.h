#ifndef TU_UTIL_H
#define TU_UTIL_H

#ifdef HAVE___BUILTIN_POPCOUNT
#define util_bitcount(i) __builtin_popcount(i)
#else
extern unsigned int
util_bitcount(unsigned int n);
#endif

#endif /* TU_UTIL_H */
