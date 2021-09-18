#include <cutils/properties.h>
#include <sync/sync.h>
#include <hardware/hardware.h>
#include <android/log.h>
#include <backtrace/Backtrace.h>

extern "C" {

/* timeout in msecs */
int sync_wait(int fd, int timeout)
{
   return 0;
}

int sync_merge(const char *name, int fd, int fd2)
{
   return 0;
}

struct sync_file_info* sync_file_info(int32_t fd)
{
   return NULL;
}

void sync_file_info_free(struct sync_file_info* info)
{
}

}
