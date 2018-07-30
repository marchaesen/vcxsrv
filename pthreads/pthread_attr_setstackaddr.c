/*
 * pthread_attr_setstackaddr.c
 *
 * Description:
 * This translation unit implements operations on thread attribute objects.
 *
 * --------------------------------------------------------------------------
 *
 *      Pthreads-win32 - POSIX Threads Library for Win32
 *      Copyright(C) 1998 John E. Bossom
 *      Copyright(C) 1999-2017, Pthreads-win32 contributors
 *
 *      Homepage: https://sourceforge.net/projects/pthreads4w/
 *
 *      The current list of contributors is contained
 *      in the file CONTRIBUTORS included with the source
 *      code distribution. The list can also be seen at the
 *      following World Wide Web location:
 *      https://sourceforge.net/p/pthreads4w/wiki/Contributors/
 *
 * This file is part of Pthreads-win32.
 *
 *    Pthreads-win32 is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    Pthreads-win32 is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with Pthreads-win32.  If not, see <http://www.gnu.org/licenses/>. *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "pthread.h"
#include "implement.h"


int
pthread_attr_setstackaddr (pthread_attr_t * attr, void *stackaddr)
     /*
      * ------------------------------------------------------
      * DOCPUBLIC
      *      Threads created with 'attr' will run on the stack
      *      starting at 'stackaddr'.
      *      Stack must be at least PTHREAD_STACK_MIN bytes.
      *
      * PARAMETERS
      *      attr
      *              pointer to an instance of pthread_attr_t
      *
      *      stackaddr
      *              the address of the stack to use
      *
      *
      * DESCRIPTION
      *      Threads created with 'attr' will run on the stack
      *      starting at 'stackaddr'.
      *      Stack must be at least PTHREAD_STACK_MIN bytes.
      *
      *      NOTES:
      *              1)      Function supported only if this macro is
      *                      defined:
      *
      *                              _POSIX_THREAD_ATTR_STACKADDR
      *
      *              2)      Create only one thread for each stack
      *                      address..
      *
      *              3)      Ensure that stackaddr is aligned.
      *
      * RESULTS
      *              0               successfully set stack address,
      *              EINVAL          'attr' is invalid
      *              ENOSYS          function not supported
      *
      * ------------------------------------------------------
      */
{
#if defined( _POSIX_THREAD_ATTR_STACKADDR ) && _POSIX_THREAD_ATTR_STACKADDR != -1

  if (ptw32_is_attr (attr) != 0)
    {
      return EINVAL;
    }

  (*attr)->stackaddr = stackaddr;
  return 0;

#else

  return ENOSYS;

#endif /* _POSIX_THREAD_ATTR_STACKADDR */
}
