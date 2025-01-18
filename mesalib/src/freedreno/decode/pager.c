/*
 * Copyright © 2018 Rob Clark <robdclark@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "pager.h"

static pid_t pager_pid;

static void
pager_death(int n)
{
   exit(0);
}

void
pager_open(void)
{
   int fd[2];

   if (pipe(fd) < 0) {
      fprintf(stderr, "Failed to create pager pipe: %m\n");
      exit(-1);
   }

   pager_pid = fork();
   if (pager_pid < 0) {
      fprintf(stderr, "Failed to fork pager: %m\n");
      exit(-1);
   }

   if (pager_pid == 0) {
      const char *less_opts;

      dup2(fd[0], STDIN_FILENO);
      close(fd[0]);
      close(fd[1]);

      less_opts = "FRSMKX";
      setenv("LESS", less_opts, 1);

      execlp("less", "less", NULL);

   } else {
      /* we want to kill the parent process when pager exits: */
      signal(SIGCHLD, pager_death);
      dup2(fd[1], STDOUT_FILENO);
      close(fd[0]);
      close(fd[1]);
   }
}

int
pager_close(void)
{
   siginfo_t status;

   close(STDOUT_FILENO);

   while (true) {
      memset(&status, 0, sizeof(status));
      if (waitid(P_PID, pager_pid, &status, WEXITED) < 0) {
         if (errno == EINTR)
            continue;
         return -errno;
      }

      return 0;
   }
}
