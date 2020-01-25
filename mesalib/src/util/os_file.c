/*
 * Copyright 2019 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "os_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>


#if defined(WIN32)
#include <io.h>
#define open _open
#define fdopen _fdopen
#define O_CREAT _O_CREAT
#define O_EXCL _O_EXCL
#define O_WRONLY _O_WRONLY
#endif


FILE *
os_file_create_unique(const char *filename, int filemode)
{
   int fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, filemode);
   if (fd == -1)
      return NULL;
   return fdopen(fd, "w");
}


#if defined(__linux__)

#include <fcntl.h>
#include <linux/kcmp.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>


static ssize_t
readN(int fd, char *buf, size_t len)
{
   int err = -ENODATA;
   size_t total = 0;
   do {
      ssize_t ret = read(fd, buf + total, len - total);

      if (ret < 0)
         ret = -errno;

      if (ret == -EINTR || ret == -EAGAIN)
         continue;

      if (ret <= 0) {
         err = ret;
         break;
      }

      total += ret;
   } while (total != len);

   return total ? (ssize_t)total : err;
}

char *
os_read_file(const char *filename)
{
   /* Note that this also serves as a slight margin to avoid a 2x grow when
    * the file is just a few bytes larger when we read it than when we
    * fstat'ed it.
    * The string's NULL terminator is also included in here.
    */
   size_t len = 64;

   int fd = open(filename, O_RDONLY);
   if (fd == -1) {
      /* errno set by open() */
      return NULL;
   }

   /* Pre-allocate a buffer at least the size of the file if we can read
    * that information.
    */
   struct stat stat;
   if (fstat(fd, &stat) == 0)
      len += stat.st_size;

   char *buf = malloc(len);
   if (!buf) {
      close(fd);
      errno = -ENOMEM;
      return NULL;
   }

   ssize_t actually_read;
   size_t offset = 0, remaining = len - 1;
   while ((actually_read = readN(fd, buf + offset, remaining)) == (ssize_t)remaining) {
      char *newbuf = realloc(buf, 2 * len);
      if (!newbuf) {
         free(buf);
         close(fd);
         errno = -ENOMEM;
         return NULL;
      }

      buf = newbuf;
      len *= 2;
      offset += actually_read;
      remaining = len - offset - 1;
   }

   close(fd);

   if (actually_read > 0)
      offset += actually_read;

   /* Final resize to actual size */
   len = offset + 1;
   char *newbuf = realloc(buf, len);
   if (!newbuf) {
      free(buf);
      errno = -ENOMEM;
      return NULL;
   }
   buf = newbuf;

   buf[offset] = '\0';

   return buf;
}

bool
os_same_file_description(int fd1, int fd2)
{
   pid_t pid = getpid();

   return syscall(SYS_kcmp, pid, pid, KCMP_FILE, fd1, fd2) == 0;
}

#else

#include "u_debug.h"

char *
os_read_file(const char *filename)
{
   errno = -ENOSYS;
   return NULL;
}

bool
os_same_file_description(int fd1, int fd2)
{
   if (fd1 == fd2)
      return true;

   debug_warn_once("Can't tell if different file descriptors reference the same"
                   " file description, false negatives might cause trouble!\n");
   return false;
}

#endif
