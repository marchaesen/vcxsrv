#ifndef ST_CB_MEMORYOBJECTS_H
#define ST_CB_MEMORYOBJECTS_H

#include "main/compiler.h"
#include "main/mtypes.h"

struct dd_function_table;
struct pipe_screen;

struct st_memory_object
{
   struct gl_memory_object Base;
   struct pipe_memory_object *memory;
};

static inline struct st_memory_object *
st_memory_object(struct gl_memory_object *obj)
{
   return (struct st_memory_object *)obj;
}

extern void
st_init_memoryobject_functions(struct dd_function_table *functions);

#endif
