#include "main/imports.h"
#include "main/mtypes.h"

#include "main/externalobjects.h"

#include "st_context.h"
#include "st_cb_memoryobjects.h"

#include "state_tracker/drm_driver.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"

static struct gl_memory_object *
st_memoryobj_alloc(struct gl_context *ctx, GLuint name)
{
   struct st_memory_object *st_obj = ST_CALLOC_STRUCT(st_memory_object);
   if (!st_obj)
      return NULL;

   _mesa_initialize_memory_object(ctx, &st_obj->Base, name);
   return &st_obj->Base;
}

static void
st_memoryobj_free(struct gl_context *ctx,
                  struct gl_memory_object *obj)
{
   _mesa_delete_memory_object(ctx, obj);
}


static void
st_import_memoryobj_fd(struct gl_context *ctx,
                       struct gl_memory_object *obj,
                       GLuint64 size,
                       int fd)
{
   struct st_memory_object *st_obj = st_memory_object(obj);
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct winsys_handle whandle;

   whandle.type = DRM_API_HANDLE_TYPE_FD;
   whandle.handle = fd;
   whandle.offset = 0;
   whandle.layer = 0;
   whandle.stride = 0;

   st_obj->memory = screen->memobj_create_from_handle(screen,
                                                      &whandle,
                                                      obj->Dedicated);

#if !defined(_WIN32)
   /* We own fd, but we no longer need it. So get rid of it */
   close(fd);
#endif
}

void
st_init_memoryobject_functions(struct dd_function_table *functions)
{
   functions->NewMemoryObject = st_memoryobj_alloc;
   functions->DeleteMemoryObject = st_memoryobj_free;
   functions->ImportMemoryObjectFd = st_import_memoryobj_fd;
}
