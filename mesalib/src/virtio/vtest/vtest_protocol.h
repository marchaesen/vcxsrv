/**************************************************************************
 *
 * Copyright (C) 2015 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef VTEST_PROTOCOL
#define VTEST_PROTOCOL

#define VTEST_DEFAULT_SOCKET_NAME "/tmp/.virgl_test"
#define VTEST_PROTOCOL_VERSION 2

/* 32-bit length field */
/* 32-bit cmd field */
#define VTEST_HDR_SIZE 2
#define VTEST_CMD_LEN 0 /* length of data */
#define VTEST_CMD_ID  1
#define VTEST_CMD_DATA_START 2

/* vtest cmds */
#define VCMD_GET_CAPS 1

#define VCMD_RESOURCE_CREATE 2
#define VCMD_RESOURCE_UNREF 3

#define VCMD_TRANSFER_GET 4
#define VCMD_TRANSFER_PUT 5

#define VCMD_SUBMIT_CMD 6

#define VCMD_RESOURCE_BUSY_WAIT 7

/* pass the process cmd line for debugging */
#define VCMD_CREATE_RENDERER 8

#define VCMD_GET_CAPS2 9
/* get caps */
/* 0 length cmd */
/* resp VCMD_GET_CAPS + caps */

#define VCMD_PING_PROTOCOL_VERSION 10

#define VCMD_PROTOCOL_VERSION 11

#define VCMD_RESOURCE_CREATE2 12
#define VCMD_TRANSFER_GET2 13
#define VCMD_TRANSFER_PUT2 14

#define VCMD_RES_CREATE_SIZE 10
#define VCMD_RES_CREATE_RES_HANDLE 0
#define VCMD_RES_CREATE_TARGET 1
#define VCMD_RES_CREATE_FORMAT 2
#define VCMD_RES_CREATE_BIND 3
#define VCMD_RES_CREATE_WIDTH 4
#define VCMD_RES_CREATE_HEIGHT 5
#define VCMD_RES_CREATE_DEPTH 6
#define VCMD_RES_CREATE_ARRAY_SIZE 7
#define VCMD_RES_CREATE_LAST_LEVEL 8
#define VCMD_RES_CREATE_NR_SAMPLES 9

#define VCMD_RES_CREATE2_SIZE 11
#define VCMD_RES_CREATE2_RES_HANDLE 0
#define VCMD_RES_CREATE2_TARGET 1
#define VCMD_RES_CREATE2_FORMAT 2
#define VCMD_RES_CREATE2_BIND 3
#define VCMD_RES_CREATE2_WIDTH 4
#define VCMD_RES_CREATE2_HEIGHT 5
#define VCMD_RES_CREATE2_DEPTH 6
#define VCMD_RES_CREATE2_ARRAY_SIZE 7
#define VCMD_RES_CREATE2_LAST_LEVEL 8
#define VCMD_RES_CREATE2_NR_SAMPLES 9
#define VCMD_RES_CREATE2_DATA_SIZE 10

#define VCMD_RES_UNREF_SIZE 1
#define VCMD_RES_UNREF_RES_HANDLE 0

#define VCMD_TRANSFER_HDR_SIZE 11
#define VCMD_TRANSFER_RES_HANDLE 0
#define VCMD_TRANSFER_LEVEL 1
#define VCMD_TRANSFER_STRIDE 2
#define VCMD_TRANSFER_LAYER_STRIDE 3
#define VCMD_TRANSFER_X 4
#define VCMD_TRANSFER_Y 5
#define VCMD_TRANSFER_Z 6
#define VCMD_TRANSFER_WIDTH 7
#define VCMD_TRANSFER_HEIGHT 8
#define VCMD_TRANSFER_DEPTH 9
#define VCMD_TRANSFER_DATA_SIZE 10

#define VCMD_TRANSFER2_HDR_SIZE 10
#define VCMD_TRANSFER2_RES_HANDLE 0
#define VCMD_TRANSFER2_LEVEL 1
#define VCMD_TRANSFER2_X 2
#define VCMD_TRANSFER2_Y 3
#define VCMD_TRANSFER2_Z 4
#define VCMD_TRANSFER2_WIDTH 5
#define VCMD_TRANSFER2_HEIGHT 6
#define VCMD_TRANSFER2_DEPTH 7
#define VCMD_TRANSFER2_DATA_SIZE 8
#define VCMD_TRANSFER2_OFFSET 9

#define VCMD_BUSY_WAIT_FLAG_WAIT 1

#define VCMD_BUSY_WAIT_SIZE 2
#define VCMD_BUSY_WAIT_HANDLE 0
#define VCMD_BUSY_WAIT_FLAGS 1

#define VCMD_PING_PROTOCOL_VERSION_SIZE 0

#define VCMD_PROTOCOL_VERSION_SIZE 1
#define VCMD_PROTOCOL_VERSION_VERSION 0

#endif
