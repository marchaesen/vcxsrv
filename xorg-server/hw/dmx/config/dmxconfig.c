/*
 * Copyright 2002-2003 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */

/** \file
 * Provides interface for reading DMX configuration files and for
 * combining that information with command-line configuration parameters. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxinput.h"
#include "dmxconfig.h"
#include "dmxparse.h"
#include "dmxlog.h"
#include "dmxcb.h"
#include "dmxstat.h"
#include "parser.h"

extern int yydebug;
extern FILE *yyin;

static char *dmxXkbRules;
static char *dmxXkbModel;
static char *dmxXkbLayout;
static char *dmxXkbVariant;
static char *dmxXkbOptions;

/** Stores lists of configuration information. */
typedef struct DMXConfigListStruct {
    const char *name;
    struct DMXConfigListStruct *next;
} DMXConfigList, *DMXConfigListPtr;

/** This stucture stores the parsed configuration information. */
typedef struct DMXConfigCmdStruct {
    const char *filename;
    const char *config;
    DMXConfigList *displays;
    DMXConfigList *inputs;
    DMXConfigList *xinputs;
} DMXConfigCmd, *DMXConfigCmdPtr;

DMXConfigEntryPtr dmxConfigEntry;
static DMXConfigCmd dmxConfigCmd;

static int dmxDisplaysFromCommandLine;

/** Make a note that \a display is the name of an X11 display that
 * should be initialized as a backend (output) display.  Called from
 * #ddxProcessArgument. */
void
dmxConfigStoreDisplay(const char *display)
{
    DMXConfigListPtr entry = malloc(sizeof(*entry));

    entry->name = strdup(display);
    entry->next = NULL;
    if (!dmxConfigCmd.displays)
        dmxConfigCmd.displays = entry;
    else {
        DMXConfigList *pt;

        for (pt = dmxConfigCmd.displays; pt->next; pt = pt->next);
        if (!pt)
            dmxLog(dmxFatal, "dmxConfigStoreDisplay: end of list non-NULL\n");
        pt->next = entry;
    }
    ++dmxDisplaysFromCommandLine;
}

/** Make a note that \a input is the name of an X11 display that should
 * be used for input (either a backend or a console input device). */
void
dmxConfigStoreInput(const char *input)
{
    DMXConfigListPtr entry = malloc(sizeof(*entry));

    entry->name = strdup(input);
    entry->next = NULL;
    if (!dmxConfigCmd.inputs)
        dmxConfigCmd.inputs = entry;
    else {
        DMXConfigList *pt;

        for (pt = dmxConfigCmd.inputs; pt->next; pt = pt->next);
        if (!pt)
            dmxLog(dmxFatal, "dmxConfigStoreInput: end of list non-NULL\n");
        pt->next = entry;
    }
}

/** Make a note that \a input is the name of an X11 display that should
 * be used for input from XInput extension devices. */
void
dmxConfigStoreXInput(const char *input)
{
    DMXConfigListPtr entry = malloc(sizeof(*entry));

    entry->name = strdup(input);
    entry->next = NULL;
    if (!dmxConfigCmd.xinputs)
        dmxConfigCmd.xinputs = entry;
    else {
        DMXConfigList *pt;

        for (pt = dmxConfigCmd.xinputs; pt->next; pt = pt->next);
        if (!pt)
            dmxLog(dmxFatal, "dmxConfigStoreXInput: end of list non-NULL\n");
        pt->next = entry;
    }
}

/** Make a note that \a file is the configuration file. */
void
dmxConfigStoreFile(const char *file)
{
    if (dmxConfigCmd.filename)
        dmxLog(dmxFatal, "Only one -configfile allowed\n");
    dmxConfigCmd.filename = strdup(file);
}

/** Make a note that \a config should be used as the configuration for
 * current instantiation of the DMX server. */
void
dmxConfigStoreConfig(const char *config)
{
    if (dmxConfigCmd.config)
        dmxLog(dmxFatal, "Only one -config allowed\n");
    dmxConfigCmd.config = strdup(config);
}

static int
dmxConfigReadFile(const char *filename, int debug)
{
    FILE *str;

    if (!(str = fopen(filename, "r")))
        return -1;
    dmxLog(dmxInfo, "Reading configuration file \"%s\"\n", filename);
    yyin = str;
    yydebug = debug;
    yyparse();
    fclose(str);
    return 0;
}

static const char *
dmxConfigMatch(const char *target, DMXConfigEntryPtr entry)
{
    DMXConfigVirtualPtr v = entry->virtual;
    const char *name = NULL;

    if (v && v->name)
        name = v->name;

    if (v && !dmxConfigCmd.config)
        return v->name ? v->name : "<noname>";
    if (!name)
        return NULL;
    if (!strcmp(name, target))
        return name;
    return NULL;
}

static DMXScreenInfo *
dmxConfigAddDisplay(const char *name,
                    int scrnWidth, int scrnHeight,
                    int scrnX, int scrnY,
                    int scrnXSign, int scrnYSign,
                    int rootWidth, int rootHeight,
                    int rootX, int rootY, int rootXSign, int rootYSign)
{
    DMXScreenInfo *dmxScreen;

    if (!(dmxScreens = reallocarray(dmxScreens, dmxNumScreens + 1,
                                    sizeof(*dmxScreens))))
        dmxLog(dmxFatal,
               "dmxConfigAddDisplay: realloc failed for screen %d (%s)\n",
               dmxNumScreens, name);

    dmxScreen = &dmxScreens[dmxNumScreens];
    memset(dmxScreen, 0, sizeof(*dmxScreen));
    dmxScreen->name = name;
    dmxScreen->index = dmxNumScreens;
    dmxScreen->scrnWidth = scrnWidth;
    dmxScreen->scrnHeight = scrnHeight;
    dmxScreen->scrnX = scrnX;
    dmxScreen->scrnY = scrnY;
    dmxScreen->scrnXSign = scrnXSign;
    dmxScreen->scrnYSign = scrnYSign;
    dmxScreen->rootWidth = rootWidth;
    dmxScreen->rootHeight = rootHeight;
    dmxScreen->rootX = rootX;
    dmxScreen->rootY = rootY;
    dmxScreen->stat = dmxStatAlloc();
    ++dmxNumScreens;
    return dmxScreen;
}

DMXInputInfo *
dmxConfigAddInput(const char *name, int core)
{
    DMXInputInfo *dmxInput;

    if (!(dmxInputs = reallocarray(dmxInputs, dmxNumInputs + 1,
                                   sizeof(*dmxInputs))))
        dmxLog(dmxFatal,
               "dmxConfigAddInput: realloc failed for input %d (%s)\n",
               dmxNumInputs, name);

    dmxInput = &dmxInputs[dmxNumInputs];

    memset(dmxInput, 0, sizeof(*dmxInput));
    dmxInput->name = name;
    dmxInput->inputIdx = dmxNumInputs;
    dmxInput->scrnIdx = -1;
    dmxInput->core = core;
    ++dmxNumInputs;
    return dmxInput;
}

static void
dmxConfigCopyFromDisplay(DMXConfigDisplayPtr d)
{
    DMXScreenInfo *dmxScreen;

    dmxScreen = dmxConfigAddDisplay(d->name,
                                    d->scrnWidth, d->scrnHeight,
                                    d->scrnX, d->scrnY,
                                    d->scrnXSign, d->scrnYSign,
                                    d->rootWidth, d->rootHeight,
                                    d->rootX, d->rootY,
                                    d->rootXSign, d->rootXSign);
    dmxScreen->where = PosAbsolute;
    dmxScreen->whereX = d->rootXOrigin;
    dmxScreen->whereY = d->rootYOrigin;
}

static void
dmxConfigCopyFromWall(DMXConfigWallPtr w)
{
    DMXConfigStringPtr pt;
    DMXScreenInfo *dmxScreen;
    int edge = dmxNumScreens;
    int last = dmxNumScreens;

    if (!w->xwall && !w->ywall) {       /* Try to make it square */
        int count;

        for (pt = w->nameList, count = 0; pt; pt = pt->next)
            ++count;
        w->xwall = sqrt(count) + .5;
    }

    for (pt = w->nameList; pt; pt = pt->next) {
        dmxScreen = dmxConfigAddDisplay(pt->string, w->width, w->height,
                                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        if (pt == w->nameList) {        /* Upper left */
            dmxScreen->where = PosAbsolute;
            dmxScreen->whereX = 0;
            dmxScreen->whereY = 0;
        }
        else if (w->xwall) {    /* Tile left to right, then top to bottom */
            if (!((dmxNumScreens - 1) % w->xwall)) {
                dmxScreen->where = PosBelow;
                dmxScreen->whereRefScreen = edge;
                edge = dmxNumScreens - 1;
            }
            else {
                dmxScreen->where = PosRightOf;
                dmxScreen->whereRefScreen = last;
            }
        }
        else {                  /* Tile top to bottom, then left to right */
            if (!((dmxNumScreens - 1) % w->ywall)) {
                dmxScreen->where = PosRightOf;
                dmxScreen->whereRefScreen = edge;
                edge = dmxNumScreens - 1;
            }
            else {
                dmxScreen->where = PosBelow;
                dmxScreen->whereRefScreen = last;
            }

        }
        last = dmxNumScreens - 1;
        if (dmxScreen->where == PosAbsolute)
            dmxLog(dmxInfo, "Added %s at %d %d\n",
                   pt->string, dmxScreen->whereX, dmxScreen->whereY);
        else
            dmxLog(dmxInfo, "Added %s %s %s\n",
                   pt->string,
                   dmxScreen->where == PosBelow ? "below" : "right of",
                   dmxScreens[dmxScreen->whereRefScreen].name);
    }
}

static void
dmxConfigCopyFromOption(DMXConfigOptionPtr o)
{
    DMXConfigStringPtr pt;
    int argc = 0;
    char **argv = NULL;

    if (serverGeneration != 1)
        return;                 /* FIXME: only do once, for now */
    if (!o || !o->string)
        return;
    for (pt = o->option; pt; pt = pt->next) {
        if (pt->string) {
            ++argc;
            argv = reallocarray(argv, argc + 1, sizeof(*argv));
            argv[argc] = (char *) pt->string;
        }
    }
    argv[0] = NULL;
    ProcessCommandLine(argc + 1, argv);
    free(argv);
}

static void
dmxConfigCopyFromParam(DMXConfigParamPtr p)
{
    const char **argv;
    int argc;

    if ((argv = dmxConfigLookupParam(p, "xkbrules", &argc)) && argc == 2) {
        dmxConfigSetXkbRules(argv[1]);
    }
    else if ((argv = dmxConfigLookupParam(p, "xkbmodel", &argc))
             && argc == 2) {
        dmxConfigSetXkbModel(argv[1]);
    }
    else if ((argv = dmxConfigLookupParam(p, "xkblayout", &argc))
             && argc == 2) {
        dmxConfigSetXkbLayout(argv[1]);
    }
    else if ((argv = dmxConfigLookupParam(p, "xkbvariant", &argc))
             && argc == 2) {
        dmxConfigSetXkbVariant(argv[1]);
    }
    else if ((argv = dmxConfigLookupParam(p, "xkboptions", &argc))
             && argc == 2) {
        dmxConfigSetXkbOptions(argv[1]);
    }
}

static void
dmxConfigCopyData(DMXConfigVirtualPtr v)
{
    DMXConfigSubPtr sub;

    if (v->dim)
        dmxSetWidthHeight(v->dim->x, v->dim->y);
    else
        dmxSetWidthHeight(0, 0);
    for (sub = v->subentry; sub; sub = sub->next) {
        switch (sub->type) {
        case dmxConfigDisplay:
            dmxConfigCopyFromDisplay(sub->display);
            break;
        case dmxConfigWall:
            dmxConfigCopyFromWall(sub->wall);
            break;
        case dmxConfigOption:
            dmxConfigCopyFromOption(sub->option);
            break;
        case dmxConfigParam:
            dmxConfigCopyFromParam(sub->param);
            break;
        default:
            dmxLog(dmxFatal,
                   "dmxConfigCopyData: not a display, wall, or value\n");
        }
    }
}

static void
dmxConfigFromCommandLine(void)
{
    DMXConfigListPtr pt;

    dmxLog(dmxInfo, "Using configuration from command line\n");
    for (pt = dmxConfigCmd.displays; pt; pt = pt->next) {
        DMXScreenInfo *dmxScreen = dmxConfigAddDisplay(pt->name,
                                                       0, 0, 0, 0, 0, 0,
                                                       0, 0, 0, 0, 0, 0);

        if (dmxNumScreens == 1) {
            dmxScreen->where = PosAbsolute;
            dmxScreen->whereX = 0;
            dmxScreen->whereY = 0;
            dmxLog(dmxInfo, "Added %s at %d %d\n",
                   dmxScreen->name, dmxScreen->whereX, dmxScreen->whereY);
        }
        else {
            dmxScreen->where = PosRightOf;
            dmxScreen->whereRefScreen = dmxNumScreens - 2;
            if (dmxScreen->whereRefScreen < 0)
                dmxScreen->whereRefScreen = 0;
            dmxLog(dmxInfo, "Added %s %s %s\n",
                   dmxScreen->name,
                   dmxScreen->where == PosBelow ? "below" : "right of",
                   dmxScreens[dmxScreen->whereRefScreen].name);
        }
    }
}

static void
dmxConfigFromConfigFile(void)
{
    DMXConfigEntryPtr pt;
    const char *name;

    for (pt = dmxConfigEntry; pt; pt = pt->next) {
        /* FIXME -- if an input is specified, use it */
        if (pt->type != dmxConfigVirtual)
            continue;
        if ((name = dmxConfigMatch(dmxConfigCmd.config, pt))) {
            dmxLog(dmxInfo, "Using configuration \"%s\"\n", name);
            dmxConfigCopyData(pt->virtual);
            return;
        }
    }
    dmxLog(dmxFatal, "Could not find configuration \"%s\" in \"%s\"\n",
           dmxConfigCmd.config, dmxConfigCmd.filename);
}

static void
dmxConfigConfigInputs(void)
{
    DMXConfigListPtr pt;

    if (dmxNumInputs)
        return;

    if (dmxConfigCmd.inputs) {  /* Use command line */
        for (pt = dmxConfigCmd.inputs; pt; pt = pt->next)
            dmxConfigAddInput(pt->name, TRUE);
    }
    else if (dmxNumScreens) {   /* Use first display */
        dmxConfigAddInput(dmxScreens[0].name, TRUE);
    }
    else {                      /* Use dummy */
        dmxConfigAddInput("dummy", TRUE);
    }

    if (dmxConfigCmd.xinputs) { /* Non-core devices from command line */
        for (pt = dmxConfigCmd.xinputs; pt; pt = pt->next)
            dmxConfigAddInput(pt->name, FALSE);
    }
}

/** Set up the appropriate global variables so that the DMX server will
 * be initialized using the configuration specified in the config file
 * and on the command line. */
void
dmxConfigConfigure(void)
{
    if (dmxConfigEntry) {
        dmxConfigFreeEntry(dmxConfigEntry);
        dmxConfigEntry = NULL;
    }
    if (dmxConfigCmd.filename) {
        if (dmxConfigCmd.displays)
            dmxLog(dmxWarning,
                   "Using configuration file \"%s\" instead of command line\n",
                   dmxConfigCmd.filename);
        dmxConfigReadFile(dmxConfigCmd.filename, 0);
        dmxConfigFromConfigFile();
    }
    else {
        if (dmxConfigCmd.config)
            dmxLog(dmxWarning,
                   "Configuration name (%s) without configuration file\n",
                   dmxConfigCmd.config);
        dmxConfigFromCommandLine();
    }
    dmxConfigConfigInputs();
}

/** This function determines the number of displays we WILL have and
 * sets MAXSCREENS to that value.  This is difficult since the number
 * depends on the command line (which is easy to count) or on the config
 * file, which has to be parsed. */
void
dmxConfigSetMaxScreens(void)
{
    static int processing = 0;

    if (processing)
        return;                 /* Prevent reentry via ProcessCommandLine */
    processing = 1;
    if (dmxConfigCmd.filename) {
        if (!dmxNumScreens)
            dmxConfigConfigure();
#ifndef MAXSCREENS
        SetMaxScreens(dmxNumScreens);
#endif
    }
    else
#ifndef MAXSCREENS
        SetMaxScreens(dmxDisplaysFromCommandLine);
#endif
    processing = 0;
}

/** This macro is used to generate the following access methods:
 * - dmxConfig{Set,Get}rules
 * - dmxConfig{Set,Get}model
 * - dmxConfig{Set,Get}layout
 * - dmxConfig{Set,Get}variant
 * - dmxConfig{Set,Get}options
 * These methods are used to read and write information about the keyboard. */

#define GEN(param,glob,def)                                                   \
 void dmxConfigSet##glob(const char *param) {                                 \
     if (dmx##glob) free((void *)dmx##glob);                                  \
     dmx##glob = strdup(param);                                               \
 }                                                                            \
 char *dmxConfigGet##glob(void) {                                             \
     return (char *)(dmx##glob ? dmx##glob : def);                            \
 }

GEN(rules, XkbRules, XKB_DFLT_RULES)
    GEN(model, XkbModel, XKB_DFLT_MODEL)
    GEN(layout, XkbLayout, XKB_DFLT_LAYOUT)
    GEN(variant, XkbVariant, XKB_DFLT_VARIANT)
    GEN(options, XkbOptions, XKB_DFLT_OPTIONS)
