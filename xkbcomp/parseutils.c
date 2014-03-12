/************************************************************
 Copyright (c) 1994 by Silicon Graphics Computer Systems, Inc.

 Permission to use, copy, modify, and distribute this
 software and its documentation for any purpose and without
 fee is hereby granted, provided that the above copyright
 notice appear in all copies and that both that copyright
 notice and this permission notice appear in supporting
 documentation, and that the name of Silicon Graphics not be 
 used in advertising or publicity pertaining to distribution 
 of the software without specific prior written permission.
 Silicon Graphics makes no representation about the suitability 
 of this software for any purpose. It is provided "as is"
 without any express or implied warranty.
 
 SILICON GRAPHICS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS 
 SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY 
 AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL SILICON
 GRAPHICS BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL 
 DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, 
 DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE 
 OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION  WITH
 THE USE OR PERFORMANCE OF THIS SOFTWARE.

 ********************************************************/

#define	DEBUG_VAR parseDebug
#include "parseutils.h"
#include "xkbpath.h"
#include <X11/keysym.h>
#include <X11/extensions/XKBgeom.h>
#include <X11/Xalloca.h>
#include <limits.h>
#include <stdlib.h>

XkbFile *rtrnValue;

ParseCommon *
AppendStmt(ParseCommon * to, ParseCommon * append)
{
    ParseCommon *start = to;

    if (append == NULL)
        return to;
    while ((to != NULL) && (to->next != NULL))
    {
        to = to->next;
    }
    if (to)
    {
        to->next = append;
        return start;
    }
    return append;
}

ExprDef *
ExprCreate(unsigned op, unsigned type)
{
    ExprDef *expr;
    expr = uTypedAlloc(ExprDef);
    if (expr)
    {
        expr->common.stmtType = StmtExpr;
        expr->common.next = NULL;
        expr->op = op;
        expr->type = type;
    }
    else
    {
        FATAL("Couldn't allocate expression in parser\n");
        /* NOTREACHED */
    }
    return expr;
}

ExprDef *
ExprCreateUnary(unsigned op, unsigned type, ExprDef * child)
{
    ExprDef *expr;
    expr = uTypedAlloc(ExprDef);
    if (expr)
    {
        expr->common.stmtType = StmtExpr;
        expr->common.next = NULL;
        expr->op = op;
        expr->type = type;
        expr->value.child = child;
    }
    else
    {
        FATAL("Couldn't allocate expression in parser\n");
        /* NOTREACHED */
    }
    return expr;
}

ExprDef *
ExprCreateBinary(unsigned op, ExprDef * left, ExprDef * right)
{
    ExprDef *expr;
    expr = uTypedAlloc(ExprDef);
    if (expr)
    {
        expr->common.stmtType = StmtExpr;
        expr->common.next = NULL;
        expr->op = op;
        if ((op == OpAssign) || (left->type == TypeUnknown))
            expr->type = right->type;
        else if ((left->type == right->type) || (right->type == TypeUnknown))
            expr->type = left->type;
        else
            expr->type = TypeUnknown;
        expr->value.binary.left = left;
        expr->value.binary.right = right;
    }
    else
    {
        FATAL("Couldn't allocate expression in parser\n");
        /* NOTREACHED */
    }
    return expr;
}

KeycodeDef *
KeycodeCreate(char *name, ExprDef * value)
{
    KeycodeDef *def;

    def = uTypedAlloc(KeycodeDef);
    if (def)
    {
        def->common.stmtType = StmtKeycodeDef;
        def->common.next = NULL;
        strncpy(def->name, name, XkbKeyNameLength);
        def->name[XkbKeyNameLength] = '\0';
        def->value = value;
    }
    else
    {
        FATAL("Couldn't allocate key name definition in parser\n");
        /* NOTREACHED */
    }
    return def;
}

KeyAliasDef *
KeyAliasCreate(char *alias, char *real)
{
    KeyAliasDef *def;

    def = uTypedAlloc(KeyAliasDef);
    if (def)
    {
        def->common.stmtType = StmtKeyAliasDef;
        def->common.next = NULL;
        strncpy(def->alias, alias, XkbKeyNameLength);
        def->alias[XkbKeyNameLength] = '\0';
        strncpy(def->real, real, XkbKeyNameLength);
        def->real[XkbKeyNameLength] = '\0';
    }
    else
    {
        FATAL("Couldn't allocate key alias definition in parser\n");
        /* NOTREACHED */
    }
    return def;
}

VModDef *
VModCreate(Atom name, ExprDef * value)
{
    VModDef *def;
    def = uTypedAlloc(VModDef);
    if (def)
    {
        def->common.stmtType = StmtVModDef;
        def->common.next = NULL;
        def->name = name;
        def->value = value;
    }
    else
    {
        FATAL("Couldn't allocate variable definition in parser\n");
        /* NOTREACHED */
    }
    return def;
}

VarDef *
VarCreate(ExprDef * name, ExprDef * value)
{
    VarDef *def;
    def = uTypedAlloc(VarDef);
    if (def)
    {
        def->common.stmtType = StmtVarDef;
        def->common.next = NULL;
        def->name = name;
        def->value = value;
    }
    else
    {
        FATAL("Couldn't allocate variable definition in parser\n");
        /* NOTREACHED */
    }
    return def;
}

VarDef *
BoolVarCreate(Atom nameToken, unsigned set)
{
    ExprDef *name, *value;

    name = ExprCreate(ExprIdent, TypeUnknown);
    name->value.str = nameToken;
    value = ExprCreate(ExprValue, TypeBoolean);
    value->value.uval = set;
    return VarCreate(name, value);
}

InterpDef *
InterpCreate(const char *sym_str, ExprDef * match)
{
    InterpDef *def;

    def = uTypedAlloc(InterpDef);
    if (def)
    {
        def->common.stmtType = StmtInterpDef;
        def->common.next = NULL;
        if (LookupKeysym(sym_str, &def->sym) == 0)
            def->ignore = True;
        else
            def->ignore = False;
        def->match = match;
    }
    else
    {
        FATAL("Couldn't allocate interp definition in parser\n");
        /* NOTREACHED */
    }
    return def;
}

KeyTypeDef *
KeyTypeCreate(Atom name, VarDef * body)
{
    KeyTypeDef *def;

    def = uTypedAlloc(KeyTypeDef);
    if (def)
    {
        def->common.stmtType = StmtKeyTypeDef;
        def->common.next = NULL;
        def->merge = MergeDefault;
        def->name = name;
        def->body = body;
    }
    else
    {
        FATAL("Couldn't allocate key type definition in parser\n");
        /* NOTREACHED */
    }
    return def;
}

SymbolsDef *
SymbolsCreate(char *keyName, ExprDef * symbols)
{
    SymbolsDef *def;

    def = uTypedAlloc(SymbolsDef);
    if (def)
    {
        def->common.stmtType = StmtSymbolsDef;
        def->common.next = NULL;
        def->merge = MergeDefault;
        bzero(def->keyName, 5);
        strncpy(def->keyName, keyName, 4);
        def->symbols = symbols;
    }
    else
    {
        FATAL("Couldn't allocate symbols definition in parser\n");
        /* NOTREACHED */
    }
    return def;
}

GroupCompatDef *
GroupCompatCreate(int group, ExprDef * val)
{
    GroupCompatDef *def;

    def = uTypedAlloc(GroupCompatDef);
    if (def)
    {
        def->common.stmtType = StmtGroupCompatDef;
        def->common.next = NULL;
        def->merge = MergeDefault;
        def->group = group;
        def->def = val;
    }
    else
    {
        FATAL("Couldn't allocate group compat definition in parser\n");
        /* NOTREACHED */
    }
    return def;
}

ModMapDef *
ModMapCreate(Atom modifier, ExprDef * keys)
{
    ModMapDef *def;

    def = uTypedAlloc(ModMapDef);
    if (def)
    {
        def->common.stmtType = StmtModMapDef;
        def->common.next = NULL;
        def->merge = MergeDefault;
        def->modifier = modifier;
        def->keys = keys;
    }
    else
    {
        FATAL("Couldn't allocate mod mask definition in parser\n");
        /* NOTREACHED */
    }
    return def;
}

IndicatorMapDef *
IndicatorMapCreate(Atom name, VarDef * body)
{
    IndicatorMapDef *def;

    def = uTypedAlloc(IndicatorMapDef);
    if (def)
    {
        def->common.stmtType = StmtIndicatorMapDef;
        def->common.next = NULL;
        def->merge = MergeDefault;
        def->name = name;
        def->body = body;
    }
    else
    {
        FATAL("Couldn't allocate indicator map definition in parser\n");
        /* NOTREACHED */
    }
    return def;
}

IndicatorNameDef *
IndicatorNameCreate(int ndx, ExprDef * name, Bool virtual)
{
    IndicatorNameDef *def;

    def = uTypedAlloc(IndicatorNameDef);
    if (def)
    {
        def->common.stmtType = StmtIndicatorNameDef;
        def->common.next = NULL;
        def->merge = MergeDefault;
        def->ndx = ndx;
        def->name = name;
        def->virtual = virtual;
    }
    else
    {
        FATAL("Couldn't allocate indicator index definition in parser\n");
        /* NOTREACHED */
    }
    return def;
}

ExprDef *
ActionCreate(Atom name, ExprDef * args)
{
    ExprDef *act;

    act = uTypedAlloc(ExprDef);
    if (act)
    {
        act->common.stmtType = StmtExpr;
        act->common.next = NULL;
        act->op = ExprActionDecl;
        act->value.action.name = name;
        act->value.action.args = args;
        return act;
    }
    FATAL("Couldn't allocate ActionDef in parser\n");
    return NULL;
}

ExprDef *
CreateKeysymList(char *sym)
{
    ExprDef *def;

    def = ExprCreate(ExprKeysymList, TypeSymbols);
    if (def)
    {
        def->value.list.nSyms = 1;
        def->value.list.szSyms = 4;
        def->value.list.syms = uTypedCalloc(4, char *);
        if (def->value.list.syms != NULL)
        {
            def->value.list.syms[0] = sym;
            return def;
        }
    }
    FATAL("Couldn't allocate expression for keysym list in parser\n");
    return NULL;
}

ShapeDef *
ShapeDeclCreate(Atom name, OutlineDef * outlines)
{
    ShapeDef *shape;
    OutlineDef *ol;

    shape = uTypedAlloc(ShapeDef);
    if (shape != NULL)
    {
        bzero(shape, sizeof(ShapeDef));
        shape->common.stmtType = StmtShapeDef;
        shape->common.next = NULL;
        shape->merge = MergeDefault;
        shape->name = name;
        shape->nOutlines = 0;
        shape->outlines = outlines;
        for (ol = outlines; ol != NULL; ol = (OutlineDef *) ol->common.next)
        {
            if (ol->nPoints > 0)
                shape->nOutlines++;
        }
    }
    return shape;
}

OutlineDef *
OutlineCreate(Atom field, ExprDef * points)
{
    OutlineDef *outline;
    ExprDef *pt;

    outline = uTypedAlloc(OutlineDef);
    if (outline != NULL)
    {
        bzero(outline, sizeof(OutlineDef));
        outline->common.stmtType = StmtOutlineDef;
        outline->common.next = NULL;
        outline->field = field;
        outline->nPoints = 0;
        if (points->op == ExprCoord)
        {
            for (pt = points; pt != NULL; pt = (ExprDef *) pt->common.next)
            {
                outline->nPoints++;
            }
        }
        outline->points = points;
    }
    return outline;
}

KeyDef *
KeyDeclCreate(char *name, ExprDef * expr)
{
    KeyDef *key;

    key = uTypedAlloc(KeyDef);
    if (key != NULL)
    {
        bzero(key, sizeof(KeyDef));
        key->common.stmtType = StmtKeyDef;
        key->common.next = NULL;
        if (name)
            key->name = name;
        else
            key->expr = expr;
    }
    return key;
}

KeyDef *
KeyDeclMerge(KeyDef * into, KeyDef * from)
{
    into->expr =
        (ExprDef *) AppendStmt(&into->expr->common, &from->expr->common);
    from->expr = NULL;
    uFree(from);
    return into;
}

RowDef *
RowDeclCreate(KeyDef * keys)
{
    RowDef *row;
    KeyDef *key;

    row = uTypedAlloc(RowDef);
    if (row != NULL)
    {
        bzero(row, sizeof(RowDef));
        row->common.stmtType = StmtRowDef;
        row->common.next = NULL;
        row->nKeys = 0;
        row->keys = keys;
        for (key = keys; key != NULL; key = (KeyDef *) key->common.next)
        {
            if (key->common.stmtType == StmtKeyDef)
                row->nKeys++;
        }
    }
    return row;
}

SectionDef *
SectionDeclCreate(Atom name, RowDef * rows)
{
    SectionDef *section;
    RowDef *row;

    section = uTypedAlloc(SectionDef);
    if (section != NULL)
    {
        bzero(section, sizeof(SectionDef));
        section->common.stmtType = StmtSectionDef;
        section->common.next = NULL;
        section->name = name;
        section->nRows = 0;
        section->rows = rows;
        for (row = rows; row != NULL; row = (RowDef *) row->common.next)
        {
            if (row->common.stmtType == StmtRowDef)
                section->nRows++;
        }
    }
    return section;
}

OverlayKeyDef *
OverlayKeyCreate(char *under, char *over)
{
    OverlayKeyDef *key;

    key = uTypedAlloc(OverlayKeyDef);
    if (key != NULL)
    {
        bzero(key, sizeof(OverlayKeyDef));
        key->common.stmtType = StmtOverlayKeyDef;
        strncpy(key->over, over, XkbKeyNameLength);
        strncpy(key->under, under, XkbKeyNameLength);
        uFree(over);
        uFree(under);
    }
    return key;
}

OverlayDef *
OverlayDeclCreate(Atom name, OverlayKeyDef * keys)
{
    OverlayDef *ol;
    OverlayKeyDef *key;

    ol = uTypedAlloc(OverlayDef);
    if (ol != NULL)
    {
        bzero(ol, sizeof(OverlayDef));
        ol->common.stmtType = StmtOverlayDef;
        ol->name = name;
        ol->keys = keys;
        for (key = keys; key != NULL;
             key = (OverlayKeyDef *) key->common.next)
        {
            ol->nKeys++;
        }
    }
    return ol;
}

DoodadDef *
DoodadCreate(unsigned type, Atom name, VarDef * body)
{
    DoodadDef *doodad;

    doodad = uTypedAlloc(DoodadDef);
    if (doodad != NULL)
    {
        bzero(doodad, sizeof(DoodadDef));
        doodad->common.stmtType = StmtDoodadDef;
        doodad->common.next = NULL;
        doodad->type = type;
        doodad->name = name;
        doodad->body = body;
    }
    return doodad;
}

ExprDef *
AppendKeysymList(ExprDef * list, char *sym)
{
    if (list->value.list.nSyms >= list->value.list.szSyms)
    {
        list->value.list.szSyms *= 2;
        list->value.list.syms = uTypedRecalloc(list->value.list.syms,
                                               list->value.list.nSyms,
                                               list->value.list.szSyms,
                                               char *);
        if (list->value.list.syms == NULL)
        {
            FATAL("Couldn't resize list of symbols for append\n");
            return NULL;
        }
    }
    list->value.list.syms[list->value.list.nSyms++] = sym;
    return list;
}

int
LookupKeysym(const char *str, KeySym * sym_rtrn)
{
    KeySym sym;
    char *tmp;

    if ((!str) || (uStrCaseCmp(str, "any") == 0)
        || (uStrCaseCmp(str, "nosymbol") == 0))
    {
        *sym_rtrn = NoSymbol;
        return 1;
    }
    else if ((uStrCaseCmp(str, "none") == 0)
             || (uStrCaseCmp(str, "voidsymbol") == 0))
    {
        *sym_rtrn = XK_VoidSymbol;
        return 1;
    }
    sym = XStringToKeysym(str);
    if (sym != NoSymbol)
    {
        *sym_rtrn = sym;
        return 1;
    }
    if (strlen(str) > 2 && str[0] == '0' && str[1] == 'x') {
        sym = strtoul(str, &tmp, 16);
        if (sym != ULONG_MAX && (!tmp || *tmp == '\0')) {
            *sym_rtrn = sym;
            return 1;
        }
    }
    return 0;
}

IncludeStmt *
IncludeCreate(char *str, unsigned merge)
{
    IncludeStmt *incl, *first;
    char *file, *map, *stmt, *tmp, *extra_data;
    char nextop;
    Bool haveSelf;

    haveSelf = False;
    incl = first = NULL;
    file = map = NULL;
    tmp = str;
    stmt = uStringDup(str);
    while ((tmp) && (*tmp))
    {
        if (XkbParseIncludeMap(&tmp, &file, &map, &nextop, &extra_data))
        {
            if ((file == NULL) && (map == NULL))
            {
                if (haveSelf)
                    goto BAIL;
                haveSelf = True;
            }
            if (first == NULL)
                first = incl = uTypedAlloc(IncludeStmt);
            else
            {
                incl->next = uTypedAlloc(IncludeStmt);
                incl = incl->next;
            }
            if (incl)
            {
                incl->common.stmtType = StmtInclude;
                incl->common.next = NULL;
                incl->merge = merge;
                incl->stmt = NULL;
                incl->file = file;
                incl->map = map;
                incl->modifier = extra_data;
                incl->path = NULL;
                incl->next = NULL;
            }
            else
            {
                WSGO("Allocation failure in IncludeCreate\n");
                ACTION("Using only part of the include\n");
                break;
            }
            if (nextop == '|')
                merge = MergeAugment;
            else
                merge = MergeOverride;
        }
        else
        {
            goto BAIL;
        }
    }
    if (first)
        first->stmt = stmt;
    else if (stmt)
        uFree(stmt);
    return first;
  BAIL:
    ERROR1("Illegal include statement \"%s\"\n", stmt);
    ACTION("Ignored\n");
    while (first)
    {
        incl = first->next;
        if (first->file)
            uFree(first->file);
        if (first->map)
            uFree(first->map);
        if (first->modifier)
            uFree(first->modifier);
        if (first->path)
            uFree(first->path);
        first->file = first->map = first->path = NULL;
        uFree(first);
        first = incl;
    }
    if (stmt)
        uFree(stmt);
    return NULL;
}

#ifdef DEBUG
void
PrintStmtAddrs(ParseCommon * stmt)
{
    fprintf(stderr, "%p", stmt);
    if (stmt)
    {
        do
        {
            fprintf(stderr, "->%p", stmt->next);
            stmt = stmt->next;
        }
        while (stmt);
    }
    fprintf(stderr, "\n");
}
#endif

static void
CheckDefaultMap(XkbFile * maps)
{
    XkbFile *dflt, *tmp;

    dflt = NULL;
    for (tmp = maps, dflt = NULL; tmp != NULL;
         tmp = (XkbFile *) tmp->common.next)
    {
        if (tmp->flags & XkbLC_Default)
        {
            if (dflt == NULL)
                dflt = tmp;
            else
            {
                if (warningLevel > 2)
                {
                    WARN1("Multiple default components in %s\n",
                          (scanFile ? scanFile : "(unknown)"));
                    ACTION2("Using %s, ignoring %s\n",
                            (dflt->name ? dflt->name : "(first)"),
                            (tmp->name ? tmp->name : "(subsequent)"));
                }
                tmp->flags &= (~XkbLC_Default);
            }
        }
    }
    return;
}

int
XKBParseFile(FILE * file, XkbFile ** pRtrn)
{
    if (file)
    {
        scan_set_file(file);
        rtrnValue = NULL;
        if (yyparse() == 0)
        {
            *pRtrn = rtrnValue;
            CheckDefaultMap(rtrnValue);
            rtrnValue = NULL;
            return 1;
        }
        *pRtrn = NULL;
        return 0;
    }
    *pRtrn = NULL;
    return 1;
}

XkbFile *
CreateXKBFile(int type, char *name, ParseCommon * defs, unsigned flags)
{
    XkbFile *file;
    static int fileID;

    file = uTypedAlloc(XkbFile);
    if (file)
    {
        XkbEnsureSafeMapName(name);
        bzero(file, sizeof(XkbFile));
        file->type = type;
        file->topName = uStringDup(name);
        file->name = name;
        file->defs = defs;
        file->id = fileID++;
        file->compiled = False;
        file->flags = flags;
    }
    return file;
}

unsigned
StmtSetMerge(ParseCommon * stmt, unsigned merge)
{
    if ((merge == MergeAltForm) && (stmt->stmtType != StmtKeycodeDef))
    {
        yyerror("illegal use of 'alternate' merge mode");
        merge = MergeDefault;
    }
    return merge;
}
