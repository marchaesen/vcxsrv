/*
 * Copyright (c) 2005 Alexander Gottwald
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE ABOVE LISTED COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name(s) of the above copyright
 * holders shall not be used in advertising or otherwise to promote the sale,
 * use or other dealings in this Software without prior written authorization.
 *
 * Authors:	Alexander Gottwald, Colin Harrison
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__
#define UNICODE
#define _UNICODE

#define PROG_NUMBER 10
#define HOST_NUMBER 10
#define MAX_MESSAGE 256
#define MAX_CAPTION 128

#include <windows.h>
#include <string>
#include <vector>
struct CConfig
{
    enum {MultiWindow, Fullscreen, Windowed, Nodecoration} window;
    enum {NoClient, StartProgram, XDMCP} client;
    bool local;
    std::string display;
    std::string localprogram;
    std::string remoteprogram;
    bool compress;
    std::string host;
    std::string user;
    std::string remotepassword;
    std::string privatekey;
    bool broadcast;
    bool indirect;
    std::string xdmcp_host;
    bool clipboard;
    bool clipboardprimary;
    bool wgl;
    bool disableac;
    bool xdmcpterminate;
    std::string extra_params;
    CConfig() : window(MultiWindow), client(NoClient), display("-1"), 
                local(false),
                remotepassword(""),
                privatekey(""),
                localprogram("xcalc"),
                remoteprogram("xterm"),
                compress(false),
                host(""),
                user(""),
                broadcast(false),
                indirect(false),
                xdmcp_host(""),
                clipboard(true),
                clipboardprimary(true),
                extra_params(),
                disableac(false),
                wgl(true),
                xdmcpterminate(false)
    {
    };
    void Load(const char * filename);
    void Save(const char * filename);
};

#endif
