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
 */
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "config.h"
#include "window/util.h"
#include <stdexcept>

xmlDocPtr CreateDocument()
{
  xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr root_node = xmlNewNode(NULL, BAD_CAST "XLaunch");
  xmlDocSetRootElement(doc, root_node);
  
  return doc;
    
}

void setAttribute(xmlNodePtr elem, const char *name, const char *value)
{
  xmlNewProp(elem, BAD_CAST name, BAD_CAST value);
}

void CConfig::Save(const char *filename)
{
    xmlDocPtr doc = CreateDocument();
    xmlNodePtr root = xmlDocGetRootElement(doc);

    switch (window)
    {
	case MultiWindow:
	    setAttribute(root, "WindowMode", "MultiWindow");
	    break;
	case Fullscreen:
	    setAttribute(root, "WindowMode", "Fullscreen");
	    break;
	default:
	case Windowed:
	    setAttribute(root, "WindowMode", "Windowed");
	    break;
	case Nodecoration:
	    setAttribute(root, "WindowMode", "Nodecoration");
	    break;
    }
    switch (client)
    {
	default:
	case NoClient:
	    setAttribute(root, "ClientMode", "NoClient");
	    break;
	case StartProgram:
	    setAttribute(root, "ClientMode", "StartProgram");
	    break;
	case XDMCP:
	    setAttribute(root, "ClientMode", "XDMCP");
	    break;
    }
    setAttribute(root, "LocalClient", local?"True":"False");
    setAttribute(root, "Display", display.c_str());
    setAttribute(root, "LocalProgram", localprogram.c_str());
    setAttribute(root, "RemoteProgram", remoteprogram.c_str());
    setAttribute(root, "RemotePassword", remotepassword.c_str());
    setAttribute(root, "PrivateKey", privatekey.c_str());
    setAttribute(root, "RemoteHost", host.c_str());
    setAttribute(root, "RemoteUser", user.c_str());
    setAttribute(root, "XDMCPHost", xdmcp_host.c_str());
    setAttribute(root, "XDMCPBroadcast", broadcast?"True":"False");
    setAttribute(root, "XDMCPIndirect", indirect?"True":"False");
    setAttribute(root, "Clipboard", clipboard?"True":"False");
    setAttribute(root, "ClipboardPrimary", clipboardprimary?"True":"False");
    setAttribute(root, "ExtraParams", extra_params.c_str());
    setAttribute(root, "Wgl", wgl?"True":"False");
    setAttribute(root, "DisableAC", disableac?"True":"False");
    setAttribute(root, "XDMCPTerminate", xdmcpterminate?"True":"False");

    xmlSaveFormatFileEnc(filename, doc, "UTF-8", 1);

    /*free the document */
    xmlFreeDoc(doc);

    /*
     *Free the global variables that may
     *have been allocated by the parser.
     */
    xmlCleanupParser();
}

BOOL getAttribute(xmlNodePtr elem, const char *name, std::string &ret)
{
  char *pVal=(char*)xmlGetProp(elem,BAD_CAST name);
  if (!pVal)
    return false;
  ret=pVal;
  return true;
}

BOOL getAttributeBool(xmlNodePtr elem, const char *name, bool &ret)
{
  const char *pVal=(char*)xmlGetProp(elem, BAD_CAST name);
  if (!pVal)
    return false;

  std::string str(pVal);

  if (str == "True")
    ret = true;
  else
    ret = false;
  return true;
}


void CConfig::Load(const char *filename)
{
  xmlDocPtr doc = xmlReadFile(filename, NULL, 0);

  xmlNodePtr root;

  if (doc == NULL)
  {
    return;
  }

  root = xmlDocGetRootElement(doc);

  std::string windowMode;
  std::string clientMode;

    if (getAttribute(root, "WindowMode", windowMode))
    {
	if (windowMode == "MultiWindow")
	    window = MultiWindow;
	else if (windowMode == "Fullscreen")
	    window = Fullscreen;
	else if (windowMode == "Windowed")
	    window = Windowed;
	else if (windowMode == "Nodecoration")
	    window = Nodecoration;
    }
    if (getAttribute(root, "ClientMode", clientMode))
    {
	if (clientMode == "NoClient")
	    client = NoClient;
	else if (clientMode == "StartProgram")
	    client = StartProgram;
	else if (clientMode == "XDMCP")
	    client = XDMCP;
    }
    
    getAttributeBool(root, "LocalClient", local);
    getAttribute(root, "Display", display);
    getAttribute(root, "LocalProgram", localprogram);
    getAttribute(root, "RemoteProgram", remoteprogram);
    getAttribute(root, "RemotePassword", remotepassword);
    getAttribute(root, "PrivateKey", privatekey);
    getAttribute(root, "RemoteHost", host);
    getAttribute(root, "RemoteUser", user);
    getAttribute(root, "XDMCPHost", xdmcp_host);
    getAttributeBool(root, "XDMCPBroadcast", broadcast);
    getAttributeBool(root, "XDMCPIndirect", indirect);
    getAttributeBool(root, "Clipboard", clipboard);
    getAttributeBool(root, "ClipboardPrimary", clipboardprimary);
    getAttribute(root, "ExtraParams", extra_params);
    getAttributeBool(root, "Wgl", wgl);
    getAttributeBool(root, "DisableAC", disableac);
    getAttributeBool(root, "XDMCPTerminate", xdmcpterminate);

    /*free the document */
    xmlFreeDoc(doc);

    /*
     *Free the global variables that may
     *have been allocated by the parser.
     */
    xmlCleanupParser();
}

