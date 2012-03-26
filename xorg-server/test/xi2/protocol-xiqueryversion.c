/**
 * Copyright © 2009 Red Hat, Inc.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice (including the next
 *  paragraph) shall be included in all copies or substantial portions of the
 *  Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

/*
 * Protocol testing for XIQueryVersion request and reply.
 *
 * Test approach:
 * Wrap WriteToClient to intercept the server's reply.
 * Repeatedly test a client/server version combination, compare version in
 * reply with versions given. Version must be equal to either
 * server version or client version, whichever is smaller.
 * Client version less than 2 must return BadValue.
 */

#include <stdint.h>
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/extensions/XI2proto.h>
#include "inputstr.h"
#include "extinit.h"            /* for XInputExtensionInit */
#include "scrnintstr.h"
#include "xiqueryversion.h"

#include "protocol-common.h"

extern XExtensionVersion XIVersion;

struct test_data {
    int major_client;
    int minor_client;
    int major_server;
    int minor_server;
};

static void
reply_XIQueryVersion(ClientPtr client, int len, char *data, void *userdata)
{
    xXIQueryVersionReply *rep = (xXIQueryVersionReply *) data;
    struct test_data *versions = (struct test_data *) userdata;
    unsigned int sver, cver, ver;

    if (client->swapped) {
        swapl(&rep->length);
        swaps(&rep->sequenceNumber);
        swaps(&rep->major_version);
        swaps(&rep->minor_version);
    }

    reply_check_defaults(rep, len, XIQueryVersion);

    assert(rep->length == 0);

    sver = versions->major_server * 1000 + versions->minor_server;
    cver = versions->major_client * 1000 + versions->minor_client;
    ver = rep->major_version * 1000 + rep->minor_version;

    assert(ver >= 2000);
    assert((sver > cver) ? ver == cver : ver == sver);
}

/**
 * Run a single test with server version smaj.smin and client
 * version cmaj.cmin. Verify that return code is equal to 'error'.
 *
 * Test is run normal, then for a swapped client.
 */
static void
request_XIQueryVersion(int smaj, int smin, int cmaj, int cmin, int error)
{
    int rc;
    struct test_data versions;
    xXIQueryVersionReq request;
    ClientRec client;

    request_init(&request, XIQueryVersion);
    client = init_client(request.length, &request);
    userdata = (void *) &versions;

    /* Change the server to support smaj.smin */
    XIVersion.major_version = smaj;
    XIVersion.minor_version = smin;

    /* remember versions we send and expect */
    versions.major_client = cmaj;
    versions.minor_client = cmin;
    versions.major_server = XIVersion.major_version;
    versions.minor_server = XIVersion.minor_version;

    request.major_version = versions.major_client;
    request.minor_version = versions.minor_client;
    rc = ProcXIQueryVersion(&client);
    assert(rc == error);

    client.swapped = TRUE;

    swaps(&request.length);
    swaps(&request.major_version);
    swaps(&request.minor_version);

    rc = SProcXIQueryVersion(&client);
    assert(rc == error);
}

/* Client version less than 2.0 must return BadValue, all other combinations
 * Success */
static void
test_XIQueryVersion(void)
{
    reply_handler = reply_XIQueryVersion;

    printf("Server version 2.0 - client versions [1..3].0\n");
    /* some simple tests to catch common errors quickly */
    request_XIQueryVersion(2, 0, 1, 0, BadValue);
    request_XIQueryVersion(2, 0, 2, 0, Success);
    request_XIQueryVersion(2, 0, 3, 0, Success);

    printf("Server version 3.0 - client versions [1..3].0\n");
    request_XIQueryVersion(3, 0, 1, 0, BadValue);
    request_XIQueryVersion(3, 0, 2, 0, Success);
    request_XIQueryVersion(3, 0, 3, 0, Success);

    printf("Server version 2.0 - client versions [1..3].[1..3]\n");
    request_XIQueryVersion(2, 0, 1, 1, BadValue);
    request_XIQueryVersion(2, 0, 2, 2, Success);
    request_XIQueryVersion(2, 0, 3, 3, Success);

    printf("Server version 2.2 - client versions [1..3].0\n");
    request_XIQueryVersion(2, 2, 1, 0, BadValue);
    request_XIQueryVersion(2, 2, 2, 0, Success);
    request_XIQueryVersion(2, 2, 3, 0, Success);

#if 0
    /* this one takes a while */
    unsigned int cmin, cmaj, smin, smaj;

    printf("Testing all combinations.\n");
    for (smaj = 2; smaj <= 0xFFFF; smaj++)
        for (smin = 0; smin <= 0xFFFF; smin++)
            for (cmin = 0; cmin <= 0xFFFF; cmin++)
                for (cmaj = 0; cmaj <= 0xFFFF; cmaj++) {
                    int error = (cmaj < 2) ? BadValue : Success;

                    request_XIQueryVersion(smaj, smin, cmaj, cmin, error);
                }

#endif

    reply_handler = NULL;
}

int
main(int argc, char **argv)
{
    init_simple();

    test_XIQueryVersion();

    return 0;
}
