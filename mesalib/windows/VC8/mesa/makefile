
MHMAKESLNFILE=mesavc10.sln
BUILDCMD=MSBuild.exe $(MHMAKESLNFILE) /t:Build

ifdef IS64
TARGETDIR=x64
else
TARGETDIR=Win32
endif
DBGBUILDCMD=$(BUILDCMD) /p:Configuration=Debug /p:Platform=$(TARGETDIR)
RELBUILDCMD=$(BUILDCMD) /p:Configuration=Release /p:Platform=$(TARGETDIR)


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
      $(wildcard ..\..\..\src\glsl\glcpp\*.c) \
      $(wildcard ..\..\..\src\glsl\glcpp\*.h) \
      $(wildcard ..\..\..\src\glsl\*.c) \
      $(wildcard ..\..\..\src\glsl\*.h) \
      $(wildcard ..\..\..\src\glsl\*.cpp) \
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
      ..\..\..\src\mesa\main\format_info.c \
      ..\..\..\src\mesa\main\format_unpack.c \
      ..\..\..\src\mesa\main\format_pack.c \
      ..\..\..\src\util\format_srgb.c \
      ..\..\..\src\mapi\glapi\gen\glapiTemp.h \
      ..\..\..\src\mesa\main\remap_helper.h \
      ..\..\..\src\mesa\drivers\dri\common\xmlpool\options.h

all: $(TARGETDIR)\Release\swrast_dri.dll $(TARGETDIR)\Debug\swrast_dri.dll

..\..\..\src\mesa\drivers\dri\common\xmlpool\options.h: ..\..\..\src\mesa\drivers\dri\common\xmlpool\t_options.h ..\..\..\src\mesa\drivers\dri\common\xmlpool\gen_xmlpool.py
	cd ..\..\..\src\mesa\drivers\dri\common\xmlpool & python gen_xmlpool.py t_options.h . > options.h

..\..\..\src\mesa\main\dispatch.h: $(wildcard ..\..\..\src\mapi\glapi\gen\*.xml) ..\..\..\src\mapi\glapi\gen\gl_table.py
	cd ..\..\..\src\mapi\glapi\gen & python gl_table.py -f gl_and_es_API.xml -m remap_table > ..\..\..\mesa\main\dispatch.h

..\..\..\src\mesa\main\glapitable.h: $(wildcard ..\..\..\src\mapi\glapi\gen\*.xml) ..\..\..\src\mapi\glapi\gen\gl_table.py
	cd ..\..\..\src\mapi\glapi\gen & python gl_table.py -f gl_and_es_API.xml > ..\..\..\mesa\main\glapitable.h

..\..\..\src\mesa\main\enums.c: $(wildcard ..\..\..\src\mapi\glapi\gen\*.xml) ..\..\..\src\mapi\glapi\gen\gl_enums.py
	cd ..\..\..\src\mapi\glapi\gen & python gl_enums.py -f gl_and_es_API.xml > ..\..\..\mesa\main\enums.c

..\..\..\src\mesa\main\api_exec.c: $(wildcard ..\..\..\src\mapi\glapi\gen\*.xml) ..\..\..\src\mapi\glapi\gen\gl_genexec.py
	cd ..\..\..\src\mapi\glapi\gen & python gl_genexec.py -f gl_and_es_API.xml > ..\..\..\mesa\main\api_exec.c

GET_HASH_GEN = ../../../src/mesa/main/get_hash_generator.py
..\..\..\src\mesa\main\get_hash.h: ..\..\..\src\mapi\glapi\gen\gl_and_es_API.xml ..\..\..\src\mesa\main\get_hash_params.py $(GET_HASH_GEN)
	python $(GET_HASH_GEN) -f $(subst $/,/,$<) > $@

GEN_FORMAT_INFO=../../../src/mesa/main/format_info.py
..\..\..\src\mesa\main\format_info.c: ..\..\..\src\mesa\main\formats.csv ..\..\..\src\mesa\main\format_parser.py $(GEN_FORMAT_INFO)
	python $(GEN_FORMAT_INFO) $< > $@

..\..\..\src\util\format_srgb.c: ..\..\..\src\util\format_srgb.py
	python $< > $@

..\..\..\src\mesa\main\format_unpack.c: ..\..\..\src\mesa\main\format_unpack.py  ..\..\..\src\mesa\main\formats.csv
	python ..\..\..\src\mesa\main\format_unpack.py  ..\..\..\src\mesa\main\formats.csv > $@ 

..\..\..\src\mesa\main\format_pack.c: ..\..\..\src\mesa\main\format_pack.py  ..\..\..\src\mesa\main\formats.csv
	python ..\..\..\src\mesa\main\format_pack.py  ..\..\..\src\mesa\main\formats.csv > $@ 

REMAP_HELPER = ../../../src/mapi/glapi/gen/remap_helper.py
..\..\..\src\mesa\main\remap_helper.h: ..\..\..\src\mapi\glapi\gen\gl_and_es_API.xml $(REMAP_HELPER)
	python $(REMAP_HELPER) -f $(subst $/,/,$<) > $@

..\..\..\src\mapi\glapi\gen\glapiTemp.h: ../../../src/mapi/glapi/gen/gl_apitemp.py
	cd  ..\..\..\src\mapi\glapi\gen & python gl_apitemp.py -f gl_and_es_API.xml > glapiTemp.h

$(TARGETDIR)\Debug\swrast_dri.dll: $(DEPS)
	$(DBGBUILDCMD)

$(TARGETDIR)\Release\swrast_dri.dll: $(DEPS)
	$(RELBUILDCMD)

clean: cleanthis

cleanthis:
	del -e Debug
	del -e Release
