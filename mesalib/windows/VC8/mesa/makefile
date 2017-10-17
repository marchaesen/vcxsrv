
MHMAKESLNFILE=mesavc10.sln
BUILDCMD=MSBuild.exe $(MHMAKESLNFILE) /t:Build

ifeq ($(IS64),1)
TARGETDIR=x64
else
TARGETDIR=Win32
endif
DBGBUILDCMD=$(BUILDCMD) /p:Configuration=Debug /p:Platform=$(TARGETDIR)
RELBUILDCMD=$(BUILDCMD) /p:Configuration=Release /p:Platform=$(TARGETDIR)

COMPILERFLEXBISONGEN:=..\..\..\src/compiler\glsl\glcpp\glcpp-lex.c\
  ..\..\..\src\compiler\glsl\glcpp\glcpp-parse.c\
  ..\..\..\src\compiler\glsl\glcpp\glcpp-parse.h\
  ..\..\..\src\compiler\glsl\glcpp\glcpp-parse.output\
  ..\..\..\src\compiler\glsl\glsl_lexer.cpp\
  ..\..\..\src\compiler\glsl\glsl_parser.cpp\
  ..\..\..\src\compiler\glsl\glsl_parser.h\
  ..\..\..\src\compiler\glsl\glsl_parser.output

PROGRAMFLEXBISONGEN:=..\..\..\src\mesa\program\lex.yy.c\
  ..\..\..\src\mesa\program\program_parse.output\
  ..\..\..\src\mesa\program\program_parse.tab.c\
  ..\..\..\src\mesa\program\program_parse.tab.h


DEPS:=$(wildcard ..\..\..\src\mesa\swrast\*.c) \
      $(wildcard ..\..\..\src\mesa\swrast\*.h) \
      $(wildcard ..\..\..\src\mesa\swrast_setup\*.c) \
      $(wildcard ..\..\..\src\mesa\swrast_setup\*.h) \
      $(wildcard ..\..\..\src\mesa\main\*.c) \
      $(wildcard ..\..\..\src\mesa\main\*.h) \
      $(wildcard ..\..\..\src\mesa\math\*.c) \
      $(wildcard ..\..\..\src\mesa\math\*.h) \
      $(wildcard ..\..\..\src\mesa\program\*.c) \
      $(wildcard ..\..\..\src\mesa\program\*.cpp) \
      $(wildcard ..\..\..\src\mesa\program\*.h) \
      $(wildcard ..\..\..\src\mesa\shader\*.c) \
      $(wildcard ..\..\..\src\mesa\shader\*.h) \
      $(wildcard ..\..\..\src\mesa\shader\slang\*.c) \
      $(wildcard ..\..\..\src\mesa\shader\slang\*.h) \
      $(wildcard ..\..\..\src\mesa\shader\slang\library\*.c) \
      $(wildcard ..\..\..\src\mesa\shader\slang\library\*.h) \
      $(wildcard ..\..\..\src\mesa\glapi\*.c) \
      $(wildcard ..\..\..\src\mesa\glapi\*.h) \
      $(wildcard ..\..\..\src\mesa\math\*.c) \
      $(wildcard ..\..\..\src\mesa\math\*.h) \
      $(wildcard ..\..\..\src\mesa\tnl\*.c) \
      $(wildcard ..\..\..\src\mesa\tnl\*.h) \
      $(wildcard ..\..\..\src\mesa\vbo\*.c) \
      $(wildcard ..\..\..\src\mesa\vbo\*.h) \
      $(wildcard ..\..\..\src\compiler\glsl\glcpp\*.c) \
      $(wildcard ..\..\..\src\compiler\glsl\glcpp\*.h) \
      $(wildcard ..\..\..\src\compiler\glsl\*.c) \
      $(wildcard ..\..\..\src\compiler\glsl\*.h) \
      $(wildcard ..\..\..\src\compiler\glsl\*.cpp) \
      $(wildcard ..\..\..\src\mesa\drivers\dri\swrast\*.c) \
      $(wildcard ..\..\..\src\mesa\drivers\dri\swrast\*.h) \
      $(wildcard ..\..\..\src\mesa\drivers\dri\common\*.c) \
      $(wildcard ..\..\..\src\mesa\drivers\dri\common\*.h) \
      $(wildcard ..\..\..\src\mesa\drivers\common\*.c) \
      $(wildcard ..\..\..\src\mesa\drivers\common\*.h) \
      ..\..\..\src\mesa\main\dispatch.h \
      ..\..\..\src\mesa\main\glapitable.h \
      ..\..\..\src\mesa\main\enums.c \
      ..\..\..\src\mesa\main\api_exec.c \
      ..\..\..\src\mesa\main\get_hash.h \
      ..\..\..\src\mesa\main\format_info.h \
      ..\..\..\src\mesa\main\format_unpack.c \
      ..\..\..\src\mesa\main\format_pack.c \
      ..\..\..\src\mesa\main\format_fallback.c \
      ..\..\..\src\util\format_srgb.c \
      ..\..\..\src\mapi\glapi\gen\glapiTemp.h \
      ..\..\..\src\mesa\main\remap_helper.h \
      ..\..\..\src\util\xmlpool\options.h \
      ..\..\..\src\compiler\glsl\ir_expression_operation.h \
      ..\..\..\src\compiler\glsl\ir_expression_operation_constant.h \
      ..\..\..\src\compiler\glsl\expression_operation_strings.h \
      ..\..\..\src\compiler\glsl\ir_expression_operation_strings.h \
      ..\..\..\src\compiler\nir\nir_opcodes.h \
			$(COMPILERFLEXBISONGEN) $(PROGRAMFLEXBISONGEN)

all: $(TARGETDIR)\Release\swrast_dri.dll $(TARGETDIR)\Debug\swrast_dri.dll

$(TARGETDIR)\Debug\swrast_dri.dll: $(DEPS)
	$(DBGBUILDCMD)

$(TARGETDIR)\Release\swrast_dri.dll: $(DEPS)
	$(RELBUILDCMD)

clean: cleanthis

cleanthis:
	del -e Debug
	del -e Release

load_makefile NORELDBG=1 makefile.srcs

