@echo off
setlocal

cd "%~dp0"

set BISON_PKGDATADIR=../../../../tools/mhmake/src/bisondata

bison.exe -v -o glsl_parser.cpp -p "_mesa_glsl_" --defines=glsl_parser.h glsl_parser.yy

bison.exe -v -o glcpp/glcpp-parse.c -d -p "glcpp_parser_" --defines=glcpp/glcpp-parse.h glcpp/glcpp-parse.y

flex.exe --nounistd -oglsl_lexer.cpp glsl_lexer.ll
flex.exe --nounistd -oglcpp/glcpp-lex.c glcpp/glcpp-lex.l

endlocal

