#include "zink_format.h"
#include "vk_format.h"

int
main(int argc, char *argv[])
{
   int ret = 0;
   for (int i = 0; i < PIPE_FORMAT_COUNT; ++i) {
      enum pipe_format pipe_fmt = i;
      VkFormat vk_fmt = zink_pipe_format_to_vk_format(i);

      /* skip unsupported formats */
      if (vk_fmt == VK_FORMAT_UNDEFINED)
         continue;

      enum pipe_format roundtrip = vk_format_to_pipe_format(vk_fmt);
      if (roundtrip != pipe_fmt) {
         fprintf(stderr, "Format does not roundtrip\n"
                         "\tgot: %s\n"
                         "\texpected: %s\n",
                         util_format_name(roundtrip),
                         util_format_name(pipe_fmt));
         ret = 1;
      }
   }
   return ret;
}
