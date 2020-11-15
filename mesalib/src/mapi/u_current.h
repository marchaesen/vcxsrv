#ifndef _U_CURRENT_H_
#define _U_CURRENT_H_

#include "c99_compat.h"
#include "util/macros.h"


#if defined(MAPI_MODE_UTIL) || defined(MAPI_MODE_GLAPI) || \
    defined(MAPI_MODE_BRIDGE)

#include "glapi/glapi.h"

#ifdef USE_ELF_TLS
#define u_current_table _glapi_tls_Dispatch
#define u_current_context _glapi_tls_Context
#else
#define u_current_table _glapi_Dispatch
#define u_current_context _glapi_Context
#endif

#define u_current_get_table_internal _glapi_get_dispatch
#define u_current_get_context_internal _glapi_get_context

#define u_current_table_tsd _gl_DispatchTSD

#else /* MAPI_MODE_UTIL || MAPI_MODE_GLAPI || MAPI_MODE_BRIDGE */

struct _glapi_table;

#ifdef USE_ELF_TLS

extern __thread struct _glapi_table *u_current_table
    __attribute__((tls_model("initial-exec")));

extern __thread void *u_current_context
    __attribute__((tls_model("initial-exec")));

#else /* USE_ELF_TLS */

extern struct _glapi_table *u_current_table;
extern void *u_current_context;

#endif /* USE_ELF_TLS */

#endif /* MAPI_MODE_UTIL || MAPI_MODE_GLAPI || MAPI_MODE_BRIDGE */

void
u_current_init(void);

void
u_current_destroy(void);

void
u_current_set_table(const struct _glapi_table *tbl);

struct _glapi_table *
u_current_get_table_internal(void);

void
u_current_set_context(const void *ptr);

void *
u_current_get_context_internal(void);

#endif /* _U_CURRENT_H_ */
