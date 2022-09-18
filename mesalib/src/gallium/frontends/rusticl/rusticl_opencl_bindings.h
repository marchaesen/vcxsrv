#include <CL/cl_icd.h>

#define DECL_CL_STRUCT(name) struct name { const cl_icd_dispatch *dispatch; }
DECL_CL_STRUCT(_cl_command_queue);
DECL_CL_STRUCT(_cl_context);
DECL_CL_STRUCT(_cl_device_id);
DECL_CL_STRUCT(_cl_event);
DECL_CL_STRUCT(_cl_kernel);
DECL_CL_STRUCT(_cl_mem);
DECL_CL_STRUCT(_cl_platform_id);
DECL_CL_STRUCT(_cl_program);
DECL_CL_STRUCT(_cl_sampler);
#undef DECL_CL_STRUCT
