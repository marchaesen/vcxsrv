LIBRARY = libpixman-1

INCLUDES += $(OBJDIR)

CSRCS =			\
	pixman.c			\
	pixman-access.c			\
	pixman-access-accessors.c	\
	pixman-bits-image.c		\
	pixman-combine32.c		\
	pixman-combine64.c		\
	pixman-conical-gradient.c	\
	pixman-cpu.c			\
	pixman-edge.c			\
	pixman-edge-accessors.c		\
	pixman-fast-path.c		\
	pixman-glyph.c			\
	pixman-general.c		\
	pixman-gradient-walker.c	\
	pixman-image.c			\
	pixman-implementation.c		\
	pixman-linear-gradient.c	\
	pixman-matrix.c			\
	pixman-noop.c			\
	pixman-radial-gradient.c	\
	pixman-region16.c		\
	pixman-region32.c		\
	pixman-solid-fill.c		\
	pixman-timer.c			\
	pixman-trap.c			\
	pixman-utils.c			\
	$(NULL)

BUILT_SOURCES =				\
	pixman-combine32.c		\
	pixman-combine32.h		\
	pixman-combine64.c		\
	pixman-combine64.h		\
	$(NULL)

$(OBJDIR)\pixman-combine32.c : pixman-combine.c.template $(OBJDIR)\pixman-combine32.h make-combine.pl
	perl make-combine.pl 8 < pixman-combine.c.template > $@
$(OBJDIR)\pixman-combine32.h : pixman-combine.h.template make-combine.pl
	perl make-combine.pl 8 < pixman-combine.h.template > $@

$(OBJDIR)\pixman-combine64.c : pixman-combine.c.template $(OBJDIR)\pixman-combine64.h make-combine.pl
	perl make-combine.pl 16 < pixman-combine.c.template > $@
$(OBJDIR)\pixman-combine64.h : pixman-combine.h.template make-combine.pl
	perl make-combine.pl 16 < pixman-combine.h.template > $@

## mmx code
CSRCS += pixman-mmx.c

# sse2 code
CSRCS += pixman-sse2.c

