/*
 * Copyright © 2014 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Hans de Goede <hdegoede@redhat.com>
 */

#include "dix-config.h"

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <drm/drm.h>
#include <xf86drm.h> /* For DRM_DEV_NAME */

#define CONFIG_FILE SYSCONFDIR "/X11/Xwrapper.config"

enum { ROOT_ONLY, CONSOLE_ONLY, ANYBODY };

/* KISS non locale / LANG parsing isspace version */
static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

static char *strip(char *s)
{
    int i;

    /* Strip leading whitespace */
    while (s[0] && is_space(s[0]))
        s++;

    /* Strip trailing whitespace */
    i = strlen(s) - 1;
    while (i >= 0 && is_space(s[i])) {
        s[i] = 0;
        i--;
    }

    return s;
}

static void parse_config(int *allowed, int *needs_root_rights)
{
    FILE *f;
    char buf[1024];
    char *stripped, *equals, *key, *value;
    int line = 0;

    f = fopen(CONFIG_FILE, "r");
    if (!f)
        return;

    while (fgets(buf, sizeof(buf), f)) {
        line++;

        /* Skip comments and empty lines */
        stripped = strip(buf);
        if (stripped[0] == '#' || stripped[0] == 0)
            continue;

        /* Split in a key + value pair */
        equals = strchr(stripped, '=');
        if (!equals) {
            fprintf(stderr, "Syntax error at %s line %d\n", CONFIG_FILE, line);
            exit(1);
        }
        *equals = 0;
        key   = strip(stripped);   /* To remove trailing whitespace from key */
        value = strip(equals + 1); /* To remove leading whitespace from val */
        if (!key[0]) {
            fprintf(stderr, "Missing key at %s line %d\n", CONFIG_FILE, line);
            exit(1);
        }
        if (!value[0]) {
            fprintf(stderr, "Missing value at %s line %d\n", CONFIG_FILE, line);
            exit(1);
        }

        /* And finally process */
        if (strcmp(key, "allowed_users") == 0) {
            if (strcmp(value, "rootonly") == 0)
                *allowed = ROOT_ONLY;
            else if (strcmp(value, "console") == 0)
                *allowed = CONSOLE_ONLY;
            else if (strcmp(value, "anybody") == 0)
                *allowed = ANYBODY;
            else {
                fprintf(stderr,
                    "Invalid value '%s' for 'allowed_users' at %s line %d\n",
                    value, CONFIG_FILE, line);
                exit(1);
            }
        }
        else if (strcmp(key, "needs_root_rights") == 0) {
            if (strcmp(value, "yes") == 0)
                *needs_root_rights = 1;
            else if (strcmp(value, "no") == 0)
                *needs_root_rights = 0;
            else if (strcmp(value, "auto") == 0)
                *needs_root_rights = -1;
            else {
                fprintf(stderr,
                    "Invalid value '%s' for 'needs_root_rights' at %s line %d\n",
                    value, CONFIG_FILE, line);
                exit(1);
            }
        }
        else if (strcmp(key, "nice_value") == 0) {
            /* Backward compatibility with older Debian Xwrapper, ignore */
        }
        else {
            fprintf(stderr, "Invalid key '%s' at %s line %d\n", key,
                    CONFIG_FILE, line);
            exit(1);
        }
    }
    fclose(f);
}

int main(int argc, char *argv[])
{
    struct drm_mode_card_res res;
    struct stat st;
    char buf[PATH_MAX];
    int i, r, fd;
    int kms_cards = 0;
    int total_cards = 0;
    int allowed = CONSOLE_ONLY;
    int needs_root_rights = -1;

    parse_config(&allowed, &needs_root_rights);

    /* For non root users check if they are allowed to run the X server */
    if (getuid() != 0) {
        switch (allowed) {
        case ROOT_ONLY:
            /* Already checked above */
            fprintf(stderr, "%s: Only root is allowed to run the X server\n", argv[0]);
            exit(1);
            break;
        case CONSOLE_ONLY:
            /* Some of stdin / stdout / stderr maybe redirected to a file */
            for (i = STDIN_FILENO; i <= STDERR_FILENO; i++) {
                r = fstat(i, &st);
                if (r == 0 && S_ISCHR(st.st_mode) && major(st.st_rdev) == 4)
                    break;
            }
            if (i > STDERR_FILENO) {
                fprintf(stderr, "%s: Only console users are allowed to run the X server\n", argv[0]);
                exit(1);
            }
            break;
        case ANYBODY:
            break;
        }
    }

    /* Detect if we need root rights, except when overriden by the config */
    if (needs_root_rights == -1) {
        for (i = 0; i < 16; i++) {
            snprintf(buf, sizeof(buf), DRM_DEV_NAME, DRM_DIR_NAME, i);
            fd = open(buf, O_RDWR);
            if (fd == -1)
                continue;

            total_cards++;

            memset(&res, 0, sizeof(struct drm_mode_card_res));
            r = ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res);
            if (r == 0 && res.count_connectors > 0)
                kms_cards++;

            close(fd);
        }
    }

    /* If we've found cards, and all cards support kms, drop root rights */
    if (needs_root_rights == 0 || (total_cards && kms_cards == total_cards)) {
        gid_t realgid = getgid();
        uid_t realuid = getuid();

        if (setresgid(-1, realgid, realgid) != 0) {
            perror("Could not drop setgid privileges");
            exit(1);
        }
        if (setresuid(-1, realuid, realuid) != 0) {
            perror("Could not drop setuid privileges");
            exit(1);
        }
    }

    snprintf(buf, sizeof(buf), "%s/Xorg.bin", SUID_WRAPPER_DIR);

    /* Check if the server is executable by our real uid */
    if (access(buf, X_OK) != 0) {
        perror("Missing execute permissions for " SUID_WRAPPER_DIR "Xorg.bin");
        exit(1);
    }

    argv[0] = buf;
    (void) execv(argv[0], argv);
    perror("Failed to execute " SUID_WRAPPER_DIR "/Xorg.bin");
    exit(1);
}
