/*
 * Copyright © 2011 Intel Corporation
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
 */
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <GL/glxtokens.h>
#include "glxserver.h"
#include "glxext.h"
#include "indirect_dispatch.h"

#define ALL_VALID_FLAGS \
    (GLX_CONTEXT_DEBUG_BIT_ARB | GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB)

static Bool
validate_GL_version(int major_version, int minor_version)
{
    if (major_version <= 0 || minor_version < 0)
        return False;

    switch (major_version) {
    case 1:
        if (minor_version > 5)
            return False;
        break;

    case 2:
        if (minor_version > 1)
            return False;
        break;

    case 3:
        if (minor_version > 3)
            return False;
        break;

    default:
        break;
    }

    return True;
}

static Bool
validate_render_type(uint32_t render_type)
{
    switch (render_type) {
    case GLX_RGBA_TYPE:
    case GLX_COLOR_INDEX_TYPE:
        return True;
    default:
        return False;
    }
}

int
__glXDisp_CreateContextAttribsARB(__GLXclientState * cl, GLbyte * pc)
{
    ClientPtr client = cl->client;
    xGLXCreateContextAttribsARBReq *req = (xGLXCreateContextAttribsARBReq *) pc;
    int32_t *attribs = (req->numAttribs != 0) ? (int32_t *) (req + 1) : NULL;
    unsigned i;
    int major_version = 1;
    int minor_version = 0;
    uint32_t flags = 0;
    uint32_t render_type = GLX_RGBA_TYPE;
    __GLXcontext *ctx = NULL;
    __GLXcontext *shareCtx = NULL;
    __GLXscreen *glxScreen;
    __GLXconfig *config;
    int err;

    /* Verify that the size of the packet matches the size inferred from the
     * sizes specified for the various fields.
     */
    const unsigned expected_size = (sz_xGLXCreateContextAttribsARBReq
                                    + (req->numAttribs * 8)) / 4;

    if (req->length != expected_size)
        return BadLength;

    LEGAL_NEW_RESOURCE(req->context, client);

    /* The GLX_ARB_create_context spec says:
     *
     *     "* If <config> is not a valid GLXFBConfig, GLXBadFBConfig is
     *        generated."
     *
     * On the client, the screen comes from the FBConfig, so GLXBadFBConfig
     * should be issued if the screen is nonsense.
     */
    if (!validGlxScreen(client, req->screen, &glxScreen, &err))
        return __glXError(GLXBadFBConfig);

    if (!validGlxFBConfig(client, glxScreen, req->fbconfig, &config, &err))
        return __glXError(GLXBadFBConfig);

    /* Validate the context with which the new context should share resources.
     */
    if (req->shareList != None) {
        if (!validGlxContext(client, req->shareList, DixReadAccess,
                             &shareCtx, &err))
            return err;

        /* The crazy condition is because C doesn't have a logical XOR
         * operator.  Comparing directly for equality may fail if one is 1 and
         * the other is 2 even though both are logically true.
         */
        if (!!req->isDirect != !!shareCtx->isDirect) {
            client->errorValue = req->shareList;
            return BadMatch;
        }

        /* The GLX_ARB_create_context spec says:
         *
         *     "* If the server context state for <share_context>...was
         *        created on a different screen than the one referenced by
         *        <config>...BadMatch is generated."
         */
        if (glxScreen != shareCtx->pGlxScreen) {
            client->errorValue = shareCtx->pGlxScreen->pScreen->myNum;
            return BadMatch;
        }
    }

    for (i = 0; i < req->numAttribs; i++) {
        switch (attribs[i * 2]) {
        case GLX_CONTEXT_MAJOR_VERSION_ARB:
            major_version = attribs[2 * i + 1];
            break;

        case GLX_CONTEXT_MINOR_VERSION_ARB:
            minor_version = attribs[2 * i + 1];
            break;

        case GLX_CONTEXT_FLAGS_ARB:
            flags = attribs[2 * i + 1];
            break;

        case GLX_RENDER_TYPE:
            render_type = attribs[2 * i + 1];
            break;

        default:
            return BadValue;
        }
    }

    /* The GLX_ARB_create_context spec says:
     *
     *     "If attributes GLX_CONTEXT_MAJOR_VERSION_ARB and
     *     GLX_CONTEXT_MINOR_VERSION_ARB, when considered together
     *     with attributes GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB and
     *     GLX_RENDER_TYPE, specify an OpenGL version and feature set
     *     that are not defined, BadMatch is generated.
     *
     *     ...Feature deprecation was introduced with OpenGL 3.0, so
     *     forward-compatible contexts may only be requested for
     *     OpenGL 3.0 and above. Thus, examples of invalid
     *     combinations of attributes include:
     *
     *       - Major version < 1 or > 3
     *       - Major version == 1 and minor version < 0 or > 5
     *       - Major version == 2 and minor version < 0 or > 1
     *       - Major version == 3 and minor version > 2
     *       - Forward-compatible flag set and major version < 3
     *       - Color index rendering and major version >= 3"
     */
    if (!validate_GL_version(major_version, minor_version))
        return BadMatch;

    if (major_version < 3
        && ((flags & GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB) != 0))
        return BadMatch;

    if (major_version >= 3 && render_type == GLX_COLOR_INDEX_TYPE)
        return BadMatch;

    if (!validate_render_type(render_type))
        return BadValue;

    if ((flags & ~ALL_VALID_FLAGS) != 0)
        return BadValue;

    /* Allocate memory for the new context
     */
    if (req->isDirect) {
        ctx = __glXdirectContextCreate(glxScreen, config, shareCtx);
        err = BadAlloc;
    }
    else {
        ctx = glxScreen->createContext(glxScreen, config, shareCtx,
                                       req->numAttribs, (uint32_t *) attribs,
                                       &err);
    }

    if (ctx == NULL)
        return err;

    ctx->pGlxScreen = glxScreen;
    ctx->config = config;
    ctx->id = req->context;
    ctx->share_id = req->shareList;
    ctx->idExists = True;
    ctx->isCurrent = False;
    ctx->isDirect = req->isDirect;
    ctx->hasUnflushedCommands = False;
    ctx->renderMode = GL_RENDER;
    ctx->feedbackBuf = NULL;
    ctx->feedbackBufSize = 0;
    ctx->selectBuf = NULL;
    ctx->selectBufSize = 0;
    ctx->drawPriv = NULL;
    ctx->readPriv = NULL;

    /* Add the new context to the various global tables of GLX contexts.
     */
    if (!__glXAddContext(ctx)) {
        (*ctx->destroy) (ctx);
        client->errorValue = req->context;
        return BadAlloc;
    }

    return Success;
}

int
__glXDispSwap_CreateContextAttribsARB(__GLXclientState * cl, GLbyte * pc)
{
    return BadRequest;
}
