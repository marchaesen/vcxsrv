                                                            -*- Autoconf -*-

# D language support for Bison

# Copyright (C) 2018-2021 Free Software Foundation, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.


# _b4_comment(TEXT, OPEN, CONTINUE, END)
# --------------------------------------
# Put TEXT in comment.  Avoid trailing spaces: don't indent empty lines.
# Avoid adding indentation to the first line, as the indentation comes
# from OPEN.  That's why we don't patsubst([$1], [^\(.\)], [   \1]).
#
# Prefix all the output lines with PREFIX.
m4_define([_b4_comment],
[$2[]m4_bpatsubst(m4_expand([[$1]]), [
\(.\)], [
$3\1])$4])


# b4_comment(TEXT, [PREFIX])
# --------------------------
# Put TEXT in comment.  Prefix all the output lines with PREFIX.
m4_define([b4_comment],
[_b4_comment([$1], [$2/* ], [$2   ], [  */])])


# b4_sync_start(LINE, FILE)
# -------------------------
m4_define([b4_sync_start], [[#]line $1 $2])


# b4_list2(LIST1, LIST2)
# ----------------------
# Join two lists with a comma if necessary.
m4_define([b4_list2],
          [$1[]m4_ifval(m4_quote($1), [m4_ifval(m4_quote($2), [[, ]])])[]$2])


# b4_percent_define_get3(DEF, PRE, POST, NOT)
# -------------------------------------------
# Expand to the value of DEF surrounded by PRE and POST if it's %define'ed,
# otherwise NOT.
m4_define([b4_percent_define_get3],
          [m4_ifval(m4_quote(b4_percent_define_get([$1])),
                [$2[]b4_percent_define_get([$1])[]$3], [$4])])


# b4_flag_value(BOOLEAN-FLAG)
# ---------------------------
m4_define([b4_flag_value], [b4_flag_if([$1], [true], [false])])


# b4_parser_class_declaration
# ---------------------------
# The declaration of the parser class ("class YYParser"), with all its
# qualifiers/annotations.
b4_percent_define_default([[api.parser.abstract]], [[false]])
b4_percent_define_default([[api.parser.final]],    [[false]])
b4_percent_define_default([[api.parser.public]],   [[false]])

m4_define([b4_parser_class_declaration],
[b4_percent_define_get3([api.parser.annotations], [], [ ])dnl
b4_percent_define_flag_if([api.parser.public],   [public ])dnl
b4_percent_define_flag_if([api.parser.abstract], [abstract ])dnl
b4_percent_define_flag_if([api.parser.final],    [final ])dnl
[class ]b4_parser_class[]dnl
b4_percent_define_get3([api.parser.extends], [ extends ])dnl
b4_percent_define_get3([api.parser.implements], [ implements ])])


# b4_lexer_if(TRUE, FALSE)
# ------------------------
m4_define([b4_lexer_if],
[b4_percent_code_ifdef([[lexer]], [$1], [$2])])


# b4_position_type_if(TRUE, FALSE)
# --------------------------------
m4_define([b4_position_type_if],
[b4_percent_define_ifdef([[position_type]], [$1], [$2])])


# b4_location_type_if(TRUE, FALSE)
# --------------------------------
m4_define([b4_location_type_if],
[b4_percent_define_ifdef([[location_type]], [$1], [$2])])


# b4_identification
# -----------------
m4_define([b4_identification],
[[/** Version number for the Bison executable that generated this parser.  */
  public static immutable string yy_bison_version = "]b4_version_string[";

  /** Name of the skeleton that generated this parser.  */
  public static immutable string yy_bison_skeleton = ]b4_skeleton[;
]])


## ------------ ##
## Data types.  ##
## ------------ ##

# b4_int_type(MIN, MAX)
# ---------------------
# Return the smallest int type able to handle numbers ranging from
# MIN to MAX (included).
m4_define([b4_int_type],
[m4_if(b4_ints_in($@,   [-128],   [127]), [1], [byte],
       b4_ints_in($@, [-32768], [32767]), [1], [short],
                                               [int])])

# b4_int_type_for(NAME)
# ---------------------
# Return the smallest int type able to handle numbers ranging from
# `NAME_min' to `NAME_max' (included).
m4_define([b4_int_type_for],
[b4_int_type($1_min, $1_max)])

# b4_null
# -------
m4_define([b4_null], [null])


# b4_integral_parser_table_define(NAME, DATA, COMMENT)
#-----------------------------------------------------
# Define "yy<TABLE-NAME>" whose contents is CONTENT.
m4_define([b4_integral_parser_table_define],
[m4_ifvaln([$3], [b4_comment([$3], [  ])])dnl
private static immutable b4_int_type_for([$2])[[]] yy$1_ =
@{
  $2
@};dnl
])


## ------------- ##
## Token kinds.  ##
## ------------- ##

m4_define([b4_symbol(-2, id)],  [[YYEMPTY]])

# b4_token_enum(TOKEN-NAME, TOKEN-NUMBER)
# ---------------------------------------
# Output the definition of this token as an enum.
m4_define([b4_token_enum],
[b4_token_format([  %s = %s,
], [$1])])

# b4_token_enums
# --------------
# Output the definition of the tokens as enums.
m4_define([b4_token_enums],
[/* Token kinds.  */
public enum TokenKind {
  ]b4_symbol_kind([-2])[ = -2,
b4_symbol_foreach([b4_token_enum])dnl
}
])



## -------------- ##
## Symbol kinds.  ##
## -------------- ##

b4_percent_define_default([[api.symbol.prefix]], [[S_]])

# b4_symbol_kind(NUM)
# -------------------
m4_define([b4_symbol_kind],
[SymbolKind.b4_symbol_kind_base($@)])


# b4_symbol_enum(SYMBOL-NUM)
# --------------------------
# Output the definition of this symbol as an enum.
m4_define([b4_symbol_enum],
[m4_format([    %-30s %s],
           m4_format([[%s = %s,]],
                     b4_symbol([$1], [kind_base]),
                     [$1]),
           [b4_symbol_tag_comment([$1])])])


# b4_declare_symbol_enum
# ----------------------
# The definition of the symbol internal numbers as an enum.
# Defining YYEMPTY here is important: it forces the compiler
# to use a signed type, which matters for yytoken.
m4_define([b4_declare_symbol_enum],
[[  /* Symbol kinds.  */
  public enum SymbolKind
  {
    ]b4_symbol(-2, kind_base)[ = -2,  /* No symbol.  */
]b4_symbol_foreach([b4_symbol_enum])dnl
[  };
]])])



# b4-case(ID, CODE, [COMMENTS])
# -----------------------------
m4_define([b4_case], [    case $1:m4_ifval([$3], [ b4_comment([$3])])
$2
      break;])


## ---------------- ##
## Default values.  ##
## ---------------- ##

m4_define([b4_yystype], [b4_percent_define_get([[stype]])])
b4_percent_define_default([[stype]], [[YYSemanticType]])])

# %name-prefix
m4_define_default([b4_prefix], [[YY]])

b4_percent_define_default([[api.parser.class]], [b4_prefix[]Parser])])
m4_define([b4_parser_class], [b4_percent_define_get([[api.parser.class]])])

#b4_percent_define_default([[location_type]], [Location])])
m4_define([b4_location_type], b4_percent_define_ifdef([[location_type]],[b4_percent_define_get([[location_type]])],[YYLocation]))

#b4_percent_define_default([[position_type]], [Position])])
m4_define([b4_position_type], b4_percent_define_ifdef([[position_type]],[b4_percent_define_get([[position_type]])],[YYPosition]))


## ----------------- ##
## Semantic Values.  ##
## ----------------- ##


# b4_symbol_value(VAL, [SYMBOL-NUM], [TYPE-TAG])
# ----------------------------------------------
# See README.  FIXME: factor in c-like?
m4_define([b4_symbol_value],
[m4_ifval([$3],
          [($1.$3)],
          [m4_ifval([$2],
                    [b4_symbol_if([$2], [has_type],
                                  [($1.b4_symbol([$2], [type]))],
                                  [$1])],
                    [$1])])])

# b4_lhs_value(SYMBOL-NUM, [TYPE])
# --------------------------------
# See README.
m4_define([b4_lhs_value],
[b4_symbol_value([yyval], [$1], [$2])])


# b4_rhs_value(RULE-LENGTH, POS, SYMBOL-NUM, [TYPE])
# --------------------------------------------------
# See README.
#
# In this simple implementation, %token and %type have class names
# between the angle brackets.
m4_define([b4_rhs_value],
[b4_symbol_value([(yystack.valueAt (b4_subtract([$1], [$2])))], [$3], [$4])])


# b4_lhs_location()
# -----------------
# Expansion of @$.
m4_define([b4_lhs_location],
[(yyloc)])


# b4_rhs_location(RULE-LENGTH, POS)
# ---------------------------------
# Expansion of @POS, where the current rule has RULE-LENGTH symbols
# on RHS.
m4_define([b4_rhs_location],
[yystack.locationAt ([$1], [$2])])


# b4_lex_param
# b4_parse_param
# --------------
# If defined, b4_lex_param arrives double quoted, but below we prefer
# it to be single quoted.  Same for b4_parse_param.

# TODO: should be in bison.m4
m4_define_default([b4_lex_param], [[]]))
m4_define([b4_lex_param], b4_lex_param))
m4_define([b4_parse_param], b4_parse_param))

# b4_lex_param_decl
# -------------------
# Extra formal arguments of the constructor.
m4_define([b4_lex_param_decl],
[m4_ifset([b4_lex_param],
          [b4_remove_comma([$1],
                           b4_param_decls(b4_lex_param))],
          [$1])])

m4_define([b4_param_decls],
          [m4_map([b4_param_decl], [$@])])
m4_define([b4_param_decl], [, $1])

m4_define([b4_remove_comma], [m4_ifval(m4_quote($1), [$1, ], [])m4_shift2($@)])



# b4_parse_param_decl
# -------------------
# Extra formal arguments of the constructor.
m4_define([b4_parse_param_decl],
[m4_ifset([b4_parse_param],
          [b4_remove_comma([$1],
                           b4_param_decls(b4_parse_param))],
          [$1])])



# b4_lex_param_call
# -------------------
# Delegating the lexer parameters to the lexer constructor.
m4_define([b4_lex_param_call],
          [m4_ifset([b4_lex_param],
                    [b4_remove_comma([$1],
                                     b4_param_calls(b4_lex_param))],
                    [$1])])
m4_define([b4_param_calls],
          [m4_map([b4_param_call], [$@])])
m4_define([b4_param_call], [, $2])



# b4_parse_param_cons
# -------------------
# Extra initialisations of the constructor.
m4_define([b4_parse_param_cons],
          [m4_ifset([b4_parse_param],
                    [b4_constructor_calls(b4_parse_param)])])

m4_define([b4_constructor_calls],
          [m4_map([b4_constructor_call], [$@])])
m4_define([b4_constructor_call],
          [this.$2 = $2;
          ])



# b4_parse_param_vars
# -------------------
# Extra instance variables.
m4_define([b4_parse_param_vars],
          [m4_ifset([b4_parse_param],
                    [
    /* User arguments.  */
b4_var_decls(b4_parse_param)])])

m4_define([b4_var_decls],
          [m4_map_sep([b4_var_decl], [
], [$@])])
m4_define([b4_var_decl],
          [    protected $1;])
