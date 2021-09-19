# D skeleton for Bison -*- autoconf -*-

# Copyright (C) 2007-2012, 2019-2021 Free Software Foundation, Inc.

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

m4_include(b4_skeletonsdir/[d.m4])


b4_output_begin([b4_parser_file_name])
b4_copyright([Skeleton implementation for Bison LALR(1) parsers in D],
             [2007-2012, 2019-2021])[
]b4_disclaimer[
]b4_percent_define_ifdef([package], [module b4_percent_define_get([package]);
])[
version(D_Version2) {
} else {
  static assert(false, "need compiler for D Version 2");
}

]b4_user_pre_prologue[
]b4_user_post_prologue[
]b4_percent_code_get([[imports]])[
import std.format;

/**
 * A Bison parser, automatically generated from <tt>]m4_bpatsubst(b4_file_name, [^"\(.*\)"$], [\1])[</tt>.
 *
 * @@author LALR (1) parser skeleton written by Paolo Bonzini.
 * Port to D language was done by Oliver Mangold.
 */

/**
 * Communication interface between the scanner and the Bison-generated
 * parser <tt>]b4_parser_class[</tt>.
 */
public interface Lexer
{]b4_locations_if([[
  /**
   * Method to retrieve the beginning position of the last scanned token.
   * @@return the position at which the last scanned token starts.  */
  @@property ]b4_position_type[ startPos ();

  /**
   * Method to retrieve the ending position of the last scanned token.
   * @@return the first position beyond the last scanned token.  */
  @@property ]b4_position_type[ endPos ();

]])[
  /**
   * Method to retrieve the semantic value of the last scanned token.
   * @@return the semantic value of the last scanned token.  */
  @@property ]b4_yystype[ semanticVal ();

  /**
   * Entry point for the scanner.  Returns the token identifier corresponding
   * to the next token and prepares to return the semantic value
   * ]b4_locations_if([and beginning/ending positions ])[of the token.
   * @@return the token identifier corresponding to the next token. */
  int yylex ();

  /**
   * Entry point for error reporting.  Emits an error
   * ]b4_locations_if([referring to the given location ])[in a user-defined way.
   *]b4_locations_if([[
   * @@param loc The location of the element to which the
   *                error message is related]])[
   * @@param s The string for the error message.  */
   void yyerror (]b4_locations_if([b4_location_type[ loc, ]])[string s);
}

]b4_locations_if([b4_position_type_if([[
static assert(__traits(compiles,
              (new ]b4_position_type[[1])[0]=(new ]b4_position_type[[1])[0]),
              "struct/class ]b4_position_type[ must be default-constructible "
              "and assignable");
static assert(__traits(compiles, (new string[1])[0]=(new ]b4_position_type[).toString()),
              "error: struct/class ]b4_position_type[ must have toString method");
]], [[
  /**
   * A struct denoting a point in the input.*/
public struct ]b4_position_type[ {

  /** The column index within the line of input.  */
  public int column = 1;
  /** The line number within an input file.  */
  public int line = 1;
  /** The name of the input file.  */
  public string filename = null;

  /**
   * A string representation of the position. */
  public string toString() const {
    if (filename)
      return format("%s:%d.%d", filename, line, column);
    else
      return format("%d.%d", line, column);
  }
}
]])b4_location_type_if([[
static assert(__traits(compiles, (new ]b4_location_type[((new ]b4_position_type[[1])[0]))) &&
              __traits(compiles, (new ]b4_location_type[((new ]b4_position_type[[1])[0], (new ]b4_position_type[[1])[0]))),
              "error: struct/class ]b4_location_type[ must have "
              "default constructor and constructors this(]b4_position_type[) and this(]b4_position_type[, ]b4_position_type[).");
static assert(__traits(compiles, (new ]b4_location_type[[1])[0].begin=(new ]b4_location_type[[1])[0].begin) &&
              __traits(compiles, (new ]b4_location_type[[1])[0].begin=(new ]b4_location_type[[1])[0].end) &&
              __traits(compiles, (new ]b4_location_type[[1])[0].end=(new ]b4_location_type[[1])[0].begin) &&
              __traits(compiles, (new ]b4_location_type[[1])[0].end=(new ]b4_location_type[[1])[0].end),
              "error: struct/class ]b4_location_type[ must have assignment-compatible "
              "members/properties 'begin' and 'end'.");
static assert(__traits(compiles, (new string[1])[0]=(new ]b4_location_type[[1])[0].toString()),
              "error: struct/class ]b4_location_type[ must have toString method.");

private immutable bool yy_location_is_class = !__traits(compiles, *(new ]b4_location_type[((new ]b4_position_type[[1])[0])));]], [[
/**
 * A class defining a pair of positions.  Positions, defined by the
 * <code>]b4_position_type[</code> class, denote a point in the input.
 * Locations represent a part of the input through the beginning
 * and ending positions.  */
public class ]b4_location_type[
{
  /** The first, inclusive, position in the range.  */
  public ]b4_position_type[ begin;

  /** The first position beyond the range.  */
  public ]b4_position_type[ end;

  /**
   * Create a <code>]b4_location_type[</code> denoting an empty range located at
   * a given point.
   * @@param loc The position at which the range is anchored.  */
  public this (]b4_position_type[ loc) {
    this.begin = this.end = loc;
  }

  public this () {
  }

  /**
   * Create a <code>]b4_location_type[</code> from the endpoints of the range.
   * @@param begin The first position included in the range.
   * @@param end   The first position beyond the range.  */
  public this (]b4_position_type[ begin, ]b4_position_type[ end)
  {
    this.begin = begin;
    this.end = end;
  }

  /**
   * A representation of the location. For this to be correct,
   * <code>]b4_position_type[</code> should override the <code>toString</code>
   * method.  */
  public override string toString () const {
    auto end_col = 0 < end.column ? end.column - 1 : 0;
    auto res = begin.toString ();
    if (end.filename && begin.filename != end.filename)
      res ~= "-" ~ format("%s:%d.%d", end.filename, end.line, end_col);
    else if (begin.line < end.line)
      res ~= "-" ~ format("%d.%d", end.line, end_col);
    else if (begin.column < end_col)
      res ~= "-" ~ format("%d", end_col);
    return res;
  }
}

private immutable bool yy_location_is_class = true;

]])])m4_ifdef([b4_user_union_members], [private union YYSemanticType
{
b4_user_union_members
};],
[m4_if(b4_tag_seen_flag, 0,
[[private alias int YYSemanticType;]])])[
]b4_token_enums[
]b4_parser_class_declaration[
{
  ]b4_identification[

]b4_declare_symbol_enum[

]b4_locations_if([[
  private final ]b4_location_type[ yylloc_from_stack (ref YYStack rhs, int n)
  {
    static if (yy_location_is_class) {
      if (n > 0)
        return new ]b4_location_type[ (rhs.locationAt (n-1).begin, rhs.locationAt (0).end);
      else
        return new ]b4_location_type[ (rhs.locationAt (0).end);
    } else {
      if (n > 0)
        return ]b4_location_type[ (rhs.locationAt (n-1).begin, rhs.locationAt (0).end);
      else
        return ]b4_location_type[ (rhs.locationAt (0).end);
    }
  }]])[

]b4_lexer_if([[  private class YYLexer implements Lexer {
]b4_percent_code_get([[lexer]])[
  }
]])[
  /** The object doing lexical analysis for us.  */
  private Lexer yylexer;

]b4_parse_param_vars[

]b4_lexer_if([[
  /**
   * Instantiate the Bison-generated parser.
   */
  public this] (b4_parse_param_decl([b4_lex_param_decl])[) {
    this (new YYLexer(]b4_lex_param_call[));
  }
]])[

  /**
   * Instantiate the Bison-generated parser.
   * @@param yylexer The scanner that will supply tokens to the parser.
   */
  ]b4_lexer_if([[protected]], [[public]]) [this (]b4_parse_param_decl([[Lexer yylexer]])[) {
    this.yylexer = yylexer;]b4_parse_trace_if([[
    this.yyDebugStream = stderr;]])[
]b4_parse_param_cons[
  }
]b4_parse_trace_if([[
  private File yyDebugStream;

  /**
   * The <tt>File</tt> on which the debugging output is
   * printed.
   */
  public File getDebugStream () { return yyDebugStream; }

  /**
   * Set the <tt>std.File</tt> on which the debug output is printed.
   * @@param s The stream that is used for debugging output.
   */
  public final void setDebugStream(File s) { yyDebugStream = s; }

  private int yydebug = 0;

  /**
   * Answer the verbosity of the debugging output; 0 means that all kinds of
   * output from the parser are suppressed.
   */
  public final int getDebugLevel() { return yydebug; }

  /**
   * Set the verbosity of the debugging output; 0 means that all kinds of
   * output from the parser are suppressed.
   * @@param level The verbosity level for debugging output.
   */
  public final void setDebugLevel(int level) { yydebug = level; }

  protected final void yycdebug (string s) {
    if (0 < yydebug)
      yyDebugStream.write (s);
  }

  protected final void yycdebugln (string s) {
    if (0 < yydebug)
      yyDebugStream.writeln (s);
  }
]])[
  private final int yylex () {
    return yylexer.yylex ();
  }

  protected final void yyerror (]b4_locations_if(ref [b4_location_type[ loc, ]])[string s) {
    yylexer.yyerror (]b4_locations_if([loc, ])[s);
  }

  /**
   * Returned by a Bison action in order to stop the parsing process and
   * return success (<tt>true</tt>).  */
  public static immutable int YYACCEPT = 0;

  /**
   * Returned by a Bison action in order to stop the parsing process and
   * return failure (<tt>false</tt>).  */
  public static immutable int YYABORT = 1;

  /**
   * Returned by a Bison action in order to start error recovery without
   * printing an error message.  */
  public static immutable int YYERROR = 2;

  // Internal return codes that are not supported for user semantic
  // actions.
  private static immutable int YYERRLAB = 3;
  private static immutable int YYNEWSTATE = 4;
  private static immutable int YYDEFAULT = 5;
  private static immutable int YYREDUCE = 6;
  private static immutable int YYERRLAB1 = 7;
  private static immutable int YYRETURN = 8;
]b4_locations_if([
  private static immutable YYSemanticType yy_semantic_null;])[
  private int yyerrstatus_ = 0;

  /**
   * Whether error recovery is being done.  In this state, the parser
   * reads token until it reaches a known state, and then restarts normal
   * operation.  */
  public final bool recovering ()
  {
    return yyerrstatus_ == 0;
  }

  private int yyaction (int yyn, ref YYStack yystack, int yylen)
  {
    ]b4_yystype[ yyval;]b4_locations_if([[
    ]b4_location_type[ yyloc = yylloc_from_stack (yystack, yylen);]])[

    /* If YYLEN is nonzero, implement the default value of the action:
       `$$ = $1'.  Otherwise, use the top of the stack.

       Otherwise, the following line sets YYVAL to garbage.
       This behavior is undocumented and Bison
       users should not rely upon it.  */
    if (yylen > 0)
      yyval = yystack.valueAt (yylen - 1);
    else
      yyval = yystack.valueAt (0);

]b4_parse_trace_if([[
    yy_reduce_print (yyn, yystack);]])[

    switch (yyn)
    {
]b4_user_actions[
      default: break;
    }

]b4_parse_trace_if([[
    import std.conv : to;
    yy_symbol_print ("-> $$ =", to!SymbolKind (yyr1_[yyn]), yyval]b4_locations_if([, yyloc])[);]])[

    yystack.pop (yylen);
    yylen = 0;

    /* Shift the result of the reduction.  */
    yyn = yyr1_[yyn];
    int yystate = yypgoto_[yyn - yyntokens_] + yystack.stateAt (0);
    if (0 <= yystate && yystate <= yylast_
        && yycheck_[yystate] == yystack.stateAt (0))
      yystate = yytable_[yystate];
    else
      yystate = yydefgoto_[yyn - yyntokens_];

    yystack.push (yystate, yyval]b4_locations_if([, yyloc])[);
    return YYNEWSTATE;
  }

  /* Return YYSTR after stripping away unnecessary quotes and
     backslashes, so that it's suitable for yyerror.  The heuristic is
     that double-quoting is unnecessary unless the string contains an
     apostrophe, a comma, or backslash (other than backslash-backslash).
     YYSTR is taken from yytname.  */
  private final string yytnamerr_ (string yystr)
  {
    if (yystr[0] == '"')
      {
        string yyr;
      strip_quotes:
        for (int i = 1; i < yystr.length; i++)
          switch (yystr[i])
            {
            case '\'':
            case ',':
              break strip_quotes;

            case '\\':
              if (yystr[++i] != '\\')
                break strip_quotes;
              goto default;
            default:
              yyr ~= yystr[i];
              break;

            case '"':
              return yyr;
            }
      }
    else if (yystr == "$end")
      return "end of input";

    return yystr;
  }
]b4_parse_trace_if([[
  /*--------------------------------.
  | Print this symbol on YYOUTPUT.  |
  `--------------------------------*/

  private final void yy_symbol_print (string s, SymbolKind yykind,
    ref ]b4_yystype[ yyvaluep]dnl
b4_locations_if([, ref ]b4_location_type[ yylocationp])[)
  {
    if (0 < yydebug)
    {
      string message = s ~ (yykind < yyntokens_ ? " token " : " nterm ")
              ~ yytname_[yykind] ~ " ("]b4_locations_if([
              ~ yylocationp.toString() ~ ": "])[;
      static if (__traits(compiles, message ~= yyvaluep.toString ()))
              message ~= yyvaluep.toString ();
      else
              message ~= format ("%s", &yyvaluep);
      message ~= ")";
      yycdebugln (message);
    }
  }
]])[
  /**
   * Parse input from the scanner that was specified at object construction
   * time.  Return whether the end of the input was reached successfully.
   *
   * @@return <tt>true</tt> if the parsing succeeds.  Note that this does not
   *          imply that there were no syntax errors.
   */
  public bool parse ()
  {
    // Lookahead token kind.
    int yychar = TokenKind.YYEMPTY;
    // Lookahead symbol kind.
    SymbolKind yytoken = ]b4_symbol(-2, kind)[;

    /* State.  */
    int yyn = 0;
    int yylen = 0;
    int yystate = 0;

    YYStack yystack;

    /* Error handling.  */
    int yynerrs_ = 0;]b4_locations_if([[
    /// The location where the error started.
    ]b4_location_type[ yyerrloc = null;

    /// ]b4_location_type[ of the lookahead.
    ]b4_location_type[ yylloc;

    /// @@$.
    ]b4_location_type[ yyloc;]])[

    /// Semantic value of the lookahead.
    ]b4_yystype[ yylval;

    bool yyresult;]b4_parse_trace_if([[

    yycdebugln ("Starting parse");]])[
    yyerrstatus_ = 0;

]m4_ifdef([b4_initial_action], [
m4_pushdef([b4_at_dollar],     [yylloc])dnl
m4_pushdef([b4_dollar_dollar], [yylval])dnl
    /* User initialization code.  */
    b4_user_initial_action
m4_popdef([b4_dollar_dollar])dnl
m4_popdef([b4_at_dollar])])dnl

  [  /* Initialize the stack.  */
    yystack.push (yystate, yylval]b4_locations_if([, yylloc])[);

    int label = YYNEWSTATE;
    for (;;)
      final switch (label)
      {
        /* New state.  Unlike in the C/C++ skeletons, the state is already
           pushed when we come here.  */
      case YYNEWSTATE:]b4_parse_trace_if([[
        yycdebugln (format("Entering state %d", yystate));
        if (0 < yydebug)
          yystack.print (yyDebugStream);]])[

        /* Accept?  */
        if (yystate == yyfinal_)
          return true;

        /* Take a decision.  First try without lookahead.  */
        yyn = yypact_[yystate];
        if (yy_pact_value_is_default_ (yyn))
        {
          label = YYDEFAULT;
          break;
        }

        /* Read a lookahead token.  */
        if (yychar == TokenKind.YYEMPTY)
        {]b4_parse_trace_if([[
          yycdebugln ("Reading a token");]])[
          yychar = yylex ();]b4_locations_if([[
          static if (yy_location_is_class) {
            yylloc = new ]b4_location_type[(yylexer.startPos, yylexer.endPos);
          } else {
            yylloc = ]b4_location_type[(yylexer.startPos, yylexer.endPos);
          }]])
          yylval = yylexer.semanticVal;[
        }

        /* Convert token to internal form.  */
        yytoken = yytranslate_ (yychar);]b4_parse_trace_if([[
        yy_symbol_print ("Next token is", yytoken, yylval]b4_locations_if([, yylloc])[);]])[

        if (yytoken == ]b4_symbol(1, kind)[)
        {
          // The scanner already issued an error message, process directly
          // to error recovery.  But do not keep the error token as
          // lookahead, it is too special and may lead us to an endless
          // loop in error recovery. */
          yychar = TokenKind.]b4_symbol(2, id)[;
          yytoken = ]b4_symbol(2, kind)[;]b4_locations_if([[
          yyerrloc = yylloc;]])[
          label = YYERRLAB1;
        }
        else
        {
          /* If the proper action on seeing token YYTOKEN is to reduce or to
             detect an error, take that action.  */
          yyn += yytoken;
          if (yyn < 0 || yylast_ < yyn || yycheck_[yyn] != yytoken)
            label = YYDEFAULT;

          /* <= 0 means reduce or error.  */
          else if ((yyn = yytable_[yyn]) <= 0)
          {
            if (yy_table_value_is_error_ (yyn))
              label = YYERRLAB;
            else
            {
              yyn = -yyn;
              label = YYREDUCE;
            }
          }
          else
          {
            /* Shift the lookahead token.  */]b4_parse_trace_if([[
            yy_symbol_print ("Shifting", yytoken, yylval]b4_locations_if([, yylloc])[);]])[

            /* Discard the token being shifted.  */
            yychar = TokenKind.YYEMPTY;

            /* Count tokens shifted since error; after three, turn off error
             * status.  */
            if (yyerrstatus_ > 0)
              --yyerrstatus_;

            yystate = yyn;
            yystack.push (yystate, yylval]b4_locations_if([, yylloc])[);
            label = YYNEWSTATE;
          }
        }
        break;

      /*-----------------------------------------------------------.
      | yydefault -- do the default action for the current state.  |
      `-----------------------------------------------------------*/
      case YYDEFAULT:
        yyn = yydefact_[yystate];
        if (yyn == 0)
          label = YYERRLAB;
        else
          label = YYREDUCE;
        break;

      /*-----------------------------.
      | yyreduce -- Do a reduction.  |
      `-----------------------------*/
      case YYREDUCE:
        yylen = yyr2_[yyn];
        label = yyaction (yyn, yystack, yylen);
        yystate = yystack.stateAt (0);
        break;

      /*--------------------------------------.
      | yyerrlab -- here on detecting error.  |
      `--------------------------------------*/
      case YYERRLAB:
        /* If not already recovering from an error, report this error.  */
        if (yyerrstatus_ == 0)
        {
          ++yynerrs_;
          if (yychar == TokenKind.]b4_symbol(-2, id)[)
            yytoken = ]b4_symbol(-2, kind)[;
          yyerror (]b4_locations_if([yylloc, ])[yysyntax_error (yystate, yytoken));
        }
]b4_locations_if([
        yyerrloc = yylloc;])[
        if (yyerrstatus_ == 3)
        {
          /* If just tried and failed to reuse lookahead token after an
           * error, discard it.  */

          if (yychar <= TokenKind.]b4_symbol(0, [id])[)
          {
            /* Return failure if at end of input.  */
            if (yychar == TokenKind.]b4_symbol(0, [id])[)
             return false;
          }
          else
            yychar = TokenKind.YYEMPTY;
        }

        /* Else will try to reuse lookahead token after shifting the error
         * token.  */
        label = YYERRLAB1;
        break;

      /*-------------------------------------------------.
      | errorlab -- error raised explicitly by YYERROR.  |
      `-------------------------------------------------*/
      case YYERROR:]b4_locations_if([
        yyerrloc = yystack.locationAt (yylen - 1);])[
        /* Do not reclaim the symbols of the rule which action triggered
           this YYERROR.  */
        yystack.pop (yylen);
        yylen = 0;
        yystate = yystack.stateAt (0);
        label = YYERRLAB1;
        break;

      /*-------------------------------------------------------------.
      | yyerrlab1 -- common code for both syntax error and YYERROR.  |
      `-------------------------------------------------------------*/
      case YYERRLAB1:
        yyerrstatus_ = 3;       /* Each real token shifted decrements this.  */

        // Pop stack until we find a state that shifts the error token.
        for (;;)
        {
          yyn = yypact_[yystate];
          if (!yy_pact_value_is_default_ (yyn))
          {
            yyn += ]b4_symbol(1, kind)[;
            if (0 <= yyn && yyn <= yylast_ && yycheck_[yyn] == ]b4_symbol(1, kind)[)
            {
              yyn = yytable_[yyn];
              if (0 < yyn)
                break;
                  }
          }

          /* Pop the current state because it cannot handle the error token.  */
          if (yystack.height == 1)
            return false;

]b4_locations_if([          yyerrloc = yystack.locationAt (0);])[
          yystack.pop ();
          yystate = yystack.stateAt (0);]b4_parse_trace_if([[
          if (0 < yydebug)
            yystack.print (yyDebugStream);]])[
        }

]b4_locations_if([
        /* Muck with the stack to setup for yylloc.  */
        yystack.push (0, yy_semantic_null, yylloc);
        yystack.push (0, yy_semantic_null, yyerrloc);
        yyloc = yylloc_from_stack (yystack, 2);
        yystack.pop (2);])[

        /* Shift the error token.  */]b4_parse_trace_if([[
        import std.conv : to;
        yy_symbol_print ("Shifting", to!SymbolKind (yystos_[yyn]), yylval]b4_locations_if([, yyloc])[);]])[
        yystate = yyn;
        yystack.push (yyn, yylval]b4_locations_if([, yyloc])[);
        label = YYNEWSTATE;
        break;

      /* Accept.  */
      case YYACCEPT:
        yyresult = true;
        label = YYRETURN;
        break;

      /* Abort.  */
      case YYABORT:
        yyresult = false;
        label = YYRETURN;
        break;

      case YYRETURN:]b4_parse_trace_if([[
        if (0 < yydebug)
          yystack.print (yyDebugStream);]])[
        return yyresult;
    }
  }

  // Generate an error message.
  private final string yysyntax_error (int yystate, SymbolKind tok)
  {]b4_parse_error_case([verbose], [[
    /* There are many possibilities here to consider:
       - Assume YYFAIL is not used.  It's too flawed to consider.
         See
         <http://lists.gnu.org/archive/html/bison-patches/2009-12/msg00024.html>
         for details.  YYERROR is fine as it does not invoke this
         function.
       - If this state is a consistent state with a default action,
         then the only way this function was invoked is if the
         default action is an error action.  In that case, don't
         check for expected tokens because there are none.
       - The only way there can be no lookahead present (in tok) is
         if this state is a consistent state with a default action.
         Thus, detecting the absence of a lookahead is sufficient to
         determine that there is no unexpected or expected token to
         report.  In that case, just report a simple "syntax error".
       - Don't assume there isn't a lookahead just because this
         state is a consistent state with a default action.  There
         might have been a previous inconsistent state, consistent
         state with a non-default action, or user semantic action
         that manipulated yychar.  (However, yychar is currently out
         of scope during semantic actions.)
       - Of course, the expected token list depends on states to
         have correct lookahead information, and it depends on the
         parser not to perform extra reductions after fetching a
         lookahead from the scanner and before detecting a syntax
         error.  Thus, state merging (from LALR or IELR) and default
         reductions corrupt the expected token list.  However, the
         list is correct for canonical LR with one exception: it
         will still contain any token that will not be accepted due
         to an error action in a later state.
      */
    if (tok != ]b4_symbol(-2, kind)[)
    {
      // FIXME: This method of building the message is not compatible
      // with internationalization.
      string res = "syntax error, unexpected ";
      res ~= yytnamerr_ (yytname_[tok]);
      int yyn = yypact_[yystate];
      if (!yy_pact_value_is_default_ (yyn))
      {
        /* Start YYX at -YYN if negative to avoid negative
           indexes in YYCHECK.  In other words, skip the first
           -YYN actions for this state because they are default
           actions.  */
        int yyxbegin = yyn < 0 ? -yyn : 0;
        /* Stay within bounds of both yycheck and yytname.  */
        int yychecklim = yylast_ - yyn + 1;
        int yyxend = yychecklim < yyntokens_ ? yychecklim : yyntokens_;
        int count = 0;
        for (int x = yyxbegin; x < yyxend; ++x)
          if (yycheck_[x + yyn] == x && x != ]b4_symbol(1, kind)[
              && !yy_table_value_is_error_ (yytable_[x + yyn]))
             ++count;
          if (count < 5)
          {
             count = 0;
             for (int x = yyxbegin; x < yyxend; ++x)
               if (yycheck_[x + yyn] == x && x != ]b4_symbol(1, kind)[
                   && !yy_table_value_is_error_ (yytable_[x + yyn]))
               {
                  res ~= count++ == 0 ? ", expecting " : " or ";
                  res ~= yytnamerr_ (yytname_[x]);
               }
          }
      }
      return res;
    }]])[
    return "syntax error";
  }

  /**
   * Whether the given <code>yypact_</code> value indicates a defaulted state.
   * @@param yyvalue   the value to check
   */
  private static bool yy_pact_value_is_default_ (int yyvalue)
  {
    return yyvalue == yypact_ninf_;
  }

  /**
   * Whether the given <code>yytable_</code> value indicates a syntax error.
   * @@param yyvalue   the value to check
   */
  private static bool yy_table_value_is_error_ (int yyvalue)
  {
    return yyvalue == yytable_ninf_;
  }

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
  private static immutable ]b4_int_type_for([b4_pact])[ yypact_ninf_ = ]b4_pact_ninf[;

  /* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule which
     number is the opposite.  If YYTABLE_NINF_, syntax error.  */
  private static immutable ]b4_int_type_for([b4_table])[ yytable_ninf_ = ]b4_table_ninf[;

  ]b4_parser_tables_define[

  /* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
     First, the terminals, then, starting at \a yyntokens_, nonterminals.  */
  private static immutable string[] yytname_ =
  @{
  ]b4_tname[
  @};

]b4_parse_trace_if([[
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
  private static immutable ]b4_int_type_for([b4_rline])[[] yyrline_ =
  @{
  ]b4_rline[
  @};

  // Report on the debug stream that the rule yyrule is going to be reduced.
  private final void yy_reduce_print (int yyrule, ref YYStack yystack)
  {
    if (yydebug == 0)
      return;

    int yylno = yyrline_[yyrule];
    int yynrhs = yyr2_[yyrule];
    /* Print the symbols being reduced, and their result.  */
    yycdebugln (format("Reducing stack by rule %d (line %d):",
                yyrule - 1, yylno));

    /* The symbols being reduced.  */
    import std.conv : to;
    for (int yyi = 0; yyi < yynrhs; yyi++)
      yy_symbol_print (format("   $%d =", yyi + 1),
                       to!SymbolKind (yystos_[yystack.stateAt(yynrhs - (yyi + 1))]),
                       ]b4_rhs_value(yynrhs, yyi + 1)b4_locations_if([,
                       b4_rhs_location(yynrhs, yyi + 1)])[);
  }
]])[

  private static SymbolKind yytranslate_ (int t)
  {
]b4_api_token_raw_if(
[[    import std.conv : to;
    return to!SymbolKind (t);]],
[[    /* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
    immutable ]b4_int_type_for([b4_translate])[[] translate_table =
    @{
  ]b4_translate[
    @};

    // Last valid token kind.
    immutable int code_max = ]b4_code_max[;

    if (t <= 0)
      return ]b4_symbol(0, kind)[;
    else if (t <= code_max)
      {
        import std.conv : to;
        return to!SymbolKind (translate_table[t]);
      }
    else
      return ]b4_symbol(2, kind)[;]])[
  }

  private static immutable int yylast_ = ]b4_last[;
  private static immutable int yynnts_ = ]b4_nterms_number[;
  private static immutable int yyfinal_ = ]b4_final_state_number[;
  private static immutable int yyntokens_ = ]b4_tokens_number[;

  private final struct YYStackElement {
    int state;
    ]b4_yystype[ value;]b4_locations_if(
    b4_location_type[[] location;])[
  }

  private final struct YYStack {
    private YYStackElement[] stack = [];

    public final @@property ulong height()
    {
      return stack.length;
    }

    public final void push (int state, ]b4_yystype[ value]dnl
  b4_locations_if([, ref ]b4_location_type[ loc])[)
    {
      stack ~= YYStackElement(state, value]b4_locations_if([, loc])[);
    }

    public final void pop ()
    {
      pop (1);
    }

    public final void pop (int num)
    {
      stack.length -= num;
    }

    public final int stateAt (int i)
    {
      return stack[$-i-1].state;
    }

]b4_locations_if([[
    public final ref ]b4_location_type[ locationAt (int i)
    {
      return stack[$-i-1].location;
    }]])[

    public final ref ]b4_yystype[ valueAt (int i)
    {
      return stack[$-i-1].value;
    }
]b4_parse_trace_if([[
    // Print the state stack on the debug stream.
    public final void print (File stream)
    {
      stream.write ("Stack now");
      for (int i = 0; i < stack.length; i++)
        stream.write (" ", stack[i].state);
      stream.writeln ();
    }]])[
  }
]b4_percent_code_get[
}
]b4_percent_code_get([[epilogue]])[]dnl
b4_epilogue[]dnl
b4_output_end
