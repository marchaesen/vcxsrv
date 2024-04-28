/* SPDX-License-Identifier: MIT OR X11
 *
 * Copyright © 2024 Enrico Weigelt, metux IT consult <info@metux.net>
 */
#ifndef _XSERVER_OS_CMDLINE_H
#define _XSERVER_OS_CMDLINE_H

#include "include/os.h"

#define CHECK_FOR_REQUIRED_ARGUMENTS(num)  \
    do if (((i + num) >= argc) || (!argv[i + num])) {                   \
        UseMsg();                                                       \
        FatalError("Required argument to %s not specified\n", argv[i]); \
    } while (0)

void UseMsg(void);
void ProcessCommandLine(int argc, char * argv[]);
void CheckUserParameters(int argc, char **argv, char **envp);

#endif /* _XSERVER_OS_CMELINE_H */
