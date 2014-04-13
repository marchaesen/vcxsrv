/*
 * Copyright © 2014 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "glamor_priv.h"
#include "glamor_transform.h"
#include "glamor_program.h"

static Bool
use_solid(PixmapPtr pixmap, GCPtr gc, glamor_program *prog, void *arg)
{
    return glamor_set_solid(pixmap, gc, TRUE, prog->fg_uniform);
}

const glamor_facet glamor_fill_solid = {
    .name = "solid",
    .fs_exec = "       gl_FragColor = fg;\n",
    .locations = glamor_program_location_fg,
    .use = use_solid,
};

static Bool
use_tile(PixmapPtr pixmap, GCPtr gc, glamor_program *prog, void *arg)
{
    return glamor_set_tiled(pixmap, gc, prog->fill_offset_uniform, prog->fill_size_uniform);
}

static const glamor_facet glamor_fill_tile = {
    .name = "tile",
    .vs_exec =  "       fill_pos = (fill_offset + primitive.xy + pos) / fill_size;\n",
    .fs_exec =  "       gl_FragColor = texture2D(sampler, fill_pos);\n",
    .locations = glamor_program_location_fill,
    .use = use_tile,
};

#if 0
static Bool
use_stipple(PixmapPtr pixmap, GCPtr gc, glamor_program *prog)
{
    return glamor_set_stippled(pixmap, gc, prog->fg_uniform, prog->fill_offset_uniform, prog->fill_size_uniform);
}

static const glamor_facet glamor_fill_stipple = {
    .name = "stipple",
    .version = 130,
    .vs_exec =  "       fill_pos = fill_offset + primitive.xy + pos;\n";
    .fs_exec = ("       if (texelFetch(sampler, ivec2(mod(fill_pos,fill_size)), 0).x == 0)\n"
                "               discard;\n"
                "       gl_FragColor = fg;\n")
    .locations = glamor_program_location_fg | glamor_program_location_fill
    .use = use_stipple,
};

static const glamor_facet glamor_fill_opaque_stipple = {
    .name = "opaque_stipple",
    .version = 130,
    .vs_exec =  "       fill_pos = fill_offset + primitive.xy + pos;\n";
    .fs_exec = ("       if (texelFetch(sampler, ivec2(mod(fill_pos,fill_size)), 0).x == 0)\n"
                "               gl_FragColor = bg;\n"
                "       else\n"
                "               gl_FragColor = fg;\n"),
    .locations = glamor_program_location_fg | glamor_program_location_bg | glamor_program_location_fill
    .use = use_opaque_stipple
};
#endif

static const glamor_facet *glamor_facet_fill[4] = {
    &glamor_fill_solid,
    &glamor_fill_tile,
    NULL,
    NULL,
};

typedef struct {
    glamor_program_location     location;
    const char                  *vs_vars;
    const char                  *fs_vars;
} glamor_location_var;

static glamor_location_var location_vars[] = {
    {
        .location = glamor_program_location_fg,
        .fs_vars = "uniform vec4 fg;\n"
    },
    {
        .location = glamor_program_location_bg,
        .fs_vars = "uniform vec4 bg;\n"
    },
    {
        .location = glamor_program_location_fill,
        .vs_vars = ("uniform vec2 fill_offset;\n"
                    "uniform vec2 fill_size;\n"
                    "varying vec2 fill_pos;\n"),
        .fs_vars = ("uniform sampler2D sampler;\n"
                    "uniform vec2 fill_size;\n"
                    "varying vec2 fill_pos;\n")
    },
    {
        .location = glamor_program_location_font,
        .fs_vars = "uniform usampler2D font;\n",
    },
};

#define NUM_LOCATION_VARS       (sizeof location_vars / sizeof location_vars[0])

static char *
add_var(char *cur, const char *add)
{
    char *new;

    if (!add)
        return cur;

    new = realloc(cur, strlen(cur) + strlen(add) + 1);
    if (!new) {
        free(cur);
        return NULL;
    }
    strcat(new, add);
    return new;
}

static char *
vs_location_vars(glamor_program_location locations)
{
    int l;
    char *vars = strdup("");

    for (l = 0; vars && l < NUM_LOCATION_VARS; l++)
        if (locations & location_vars[l].location)
            vars = add_var(vars, location_vars[l].vs_vars);
    return vars;
}

static char *
fs_location_vars(glamor_program_location locations)
{
    int l;
    char *vars = strdup("");

    for (l = 0; vars && l < NUM_LOCATION_VARS; l++)
        if (locations & location_vars[l].location)
            vars = add_var(vars, location_vars[l].fs_vars);
    return vars;
}

static const char vs_template[] =
    "%s"                                /* version */
    "%s"                                /* prim vs_vars */
    "%s"                                /* fill vs_vars */
    "%s"                                /* location vs_vars */
    GLAMOR_DECLARE_MATRIX
    "void main() {\n"
    "%s"                                /* prim vs_exec, outputs 'pos' and gl_Position */
    "%s"                                /* fill vs_exec */
    "}\n";

static const char fs_template[] =
    "%s"                                /* version */
    GLAMOR_DEFAULT_PRECISION
    "%s"                                /* prim fs_vars */
    "%s"                                /* fill fs_vars */
    "%s"                                /* location fs_vars */
    "void main() {\n"
    "%s"                                /* prim fs_exec */
    "%s"                                /* fill fs_exec */
    "}\n";

static const char *
str(const char *s)
{
    if (!s)
        return "";
    return s;
}

static const glamor_facet facet_null_fill = {
    .name = ""
};

static GLint
glamor_get_uniform(glamor_program               *prog,
                   glamor_program_location      location,
                   const char                   *name)
{
    GLint uniform;
    if (location && (prog->locations & location) == 0)
        return -2;
    uniform = glGetUniformLocation(prog->prog, name);
#if DBG
    ErrorF("%s uniform %d\n", name, uniform);
#endif
    return uniform;
}

Bool
glamor_build_program(ScreenPtr          screen,
                     glamor_program     *prog,
                     const glamor_facet *prim,
                     const glamor_facet *fill)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);

    glamor_program_location     locations = prim->locations;
    glamor_program_flag         flags = prim->flags;

    int                         version = prim->version;
    char                        *version_string = NULL;

    char                        *fs_vars = NULL;
    char                        *vs_vars = NULL;

    char                        *vs_prog_string;
    char                        *fs_prog_string;

    GLint                       fs_prog, vs_prog;

    if (!fill)
        fill = &facet_null_fill;

    locations |= fill->locations;
    flags |= fill->flags;
    version = MAX(version, fill->version);

    if (version > glamor_priv->glsl_version)
        goto fail;

    vs_vars = vs_location_vars(locations);
    fs_vars = fs_location_vars(locations);

    if (!vs_vars)
        goto fail;
    if (!fs_vars)
        goto fail;

    if (version) {
        if (asprintf(&version_string, "#version %d\n", version) < 0)
            version_string = NULL;
        if (!version_string)
            goto fail;
    }

    if (asprintf(&vs_prog_string,
                 vs_template,
                 str(version_string),
                 str(prim->vs_vars),
                 str(fill->vs_vars),
                 vs_vars,
                 str(prim->vs_exec),
                 str(fill->vs_exec)) < 0)
        vs_prog_string = NULL;

    if (asprintf(&fs_prog_string,
                 fs_template,
                 str(version_string),
                 str(prim->fs_vars),
                 str(fill->fs_vars),
                 fs_vars,
                 str(prim->fs_exec),
                 str(fill->fs_exec)) < 0)
        fs_prog_string = NULL;

    if (!vs_prog_string || !fs_prog_string)
        goto fail;

#define DBG 0
#if DBG
    ErrorF("\nPrograms for %s %s\nVertex shader:\n\n%s\n\nFragment Shader:\n\n%s",
           prim->name, fill->name, vs_prog_string, fs_prog_string);
#endif

    prog->prog = glCreateProgram();
    prog->flags = flags;
    prog->locations = locations;
    prog->prim_use = prim->use;
    prog->fill_use = fill->use;

    vs_prog = glamor_compile_glsl_prog(GL_VERTEX_SHADER, vs_prog_string);
    fs_prog = glamor_compile_glsl_prog(GL_FRAGMENT_SHADER, fs_prog_string);
    free(vs_prog_string);
    free(fs_prog_string);
    glAttachShader(prog->prog, vs_prog);
    glDeleteShader(vs_prog);
    glAttachShader(prog->prog, fs_prog);
    glDeleteShader(fs_prog);
    glBindAttribLocation(prog->prog, GLAMOR_VERTEX_POS, "primitive");

    if (prim->source_name) {
#if DBG
        ErrorF("Bind GLAMOR_VERTEX_SOURCE to %s\n", prim->source_name);
#endif
        glBindAttribLocation(prog->prog, GLAMOR_VERTEX_SOURCE, prim->source_name);
    }

    glamor_link_glsl_prog(screen, prog->prog, "%s_%s", prim->name, fill->name);

    prog->matrix_uniform = glamor_get_uniform(prog, glamor_program_location_none, "v_matrix");
    prog->fg_uniform = glamor_get_uniform(prog, glamor_program_location_fg, "fg");
    prog->bg_uniform = glamor_get_uniform(prog, glamor_program_location_bg, "bg");
    prog->fill_offset_uniform = glamor_get_uniform(prog, glamor_program_location_fill, "fill_offset");
    prog->fill_size_uniform = glamor_get_uniform(prog, glamor_program_location_fill, "fill_size");
    prog->font_uniform = glamor_get_uniform(prog, glamor_program_location_font, "font");

    if (glGetError() != GL_NO_ERROR)
        goto fail;

    free(version_string);
    free(fs_vars);
    free(vs_vars);
    return TRUE;
fail:
    prog->failed = 1;
    if (prog->prog) {
        glDeleteProgram(prog->prog);
        prog->prog = 0;
    }
    free(version_string);
    free(fs_vars);
    free(vs_vars);
    return FALSE;
}

Bool
glamor_use_program(PixmapPtr            pixmap,
                   GCPtr                gc,
                   glamor_program       *prog,
                   void                 *arg)
{
    glUseProgram(prog->prog);

    if (prog->prim_use && !prog->prim_use(pixmap, gc, prog, arg))
        return FALSE;

    if (prog->fill_use && !prog->fill_use(pixmap, gc, prog, arg))
        return FALSE;

    return TRUE;
}

glamor_program *
glamor_use_program_fill(PixmapPtr               pixmap,
                        GCPtr                   gc,
                        glamor_program_fill     *program_fill,
                        const glamor_facet      *prim)
{
    ScreenPtr                   screen = pixmap->drawable.pScreen;
    glamor_program              *prog = &program_fill->progs[gc->fillStyle];

    int                         fill_style = gc->fillStyle;
    const glamor_facet          *fill;

    if (prog->failed)
        return FALSE;

    if (!prog->prog) {
        fill = glamor_facet_fill[fill_style];
        if (!fill)
            return NULL;

        if (!glamor_build_program(screen, prog, prim, fill))
            return NULL;
    }

    if (!glamor_use_program(pixmap, gc, prog, NULL))
        return NULL;

    return prog;
}
