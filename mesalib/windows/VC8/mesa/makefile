ifdef VS2008
MHMAKESLNFILE=mesa.sln
BUILDCMD=vcbuild $(MHMAKESLNFILE)
else
MHMAKESLNFILE=mesavc10.sln
BUILDCMD=devenv.com $(MHMAKESLNFILE) /build
endif

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
      ..\..\..\src\mesa\main\enums.c

all: Release\swrast_dri.dll Debug\swrast_dri.dll

..\..\..\src\mesa\main\dispatch.h: $(wildcard ..\..\..\src\mapi\glapi\gen\*.xml)
	cd ..\..\..\src\mapi\glapi\gen & python gl_table.py -m remap_table > ..\..\..\mesa\main\dispatch.h

..\..\..\src\mesa\main\glapitable.h: $(wildcard ..\..\..\src\mapi\glapi\gen\*.xml)
	cd ..\..\..\src\mapi\glapi\gen & python gl_table.py > ..\..\..\mesa\main\glapitable.h

..\..\..\src\mesa\main\enums.c: $(wildcard ..\..\..\src\mapi\glapi\gen\*.xml)
	cd ..\..\..\src\mapi\glapi\gen & python gl_enums.py -f gl_and_es_API.xml > ..\..\..\mesa\main\enums.c

Debug\swrast_dri.dll: $(DEPS)
	$(BUILDCMD) "Debug|Win32" 

Release\swrast_dri.dll: $(DEPS)
	$(BUILDCMD) "Release|Win32" 

clean: cleanthis

cleanthis:
	del -e Debug
	del -e Release
