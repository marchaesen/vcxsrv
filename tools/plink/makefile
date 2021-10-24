ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1 on the command line or as environment variable)
endif

DEFINES += SECURITY_WIN32
INCLUDES := windows $(INCLUDES) terminal

INCLUDELIBFILES =  \
 windows\$(OBJDIR)\libwindows.lib \
 utils\$(OBJDIR)\libutils.lib \
 otherbackends\$(OBJDIR)\libotherbackends.lib \
 ssh\$(OBJDIR)\libssh.lib \
 crypto\$(OBJDIR)\libcrypto.lib

CSRCS = settings.c \
 logging.c \
 sshrand.c \
 cmdline.c \
 timing.c \
 proxy.c \
 sshproxy.c \
 sshpubk.c \
 cproxy.c \
 pinger.c \
 ldisc.c \
 callback.c \
 errsock.c \
 noterm.c \
 be_all.c \
 be_misc.c \
 nullplug.c \
 clicons.c \
 console.c \
 x11disp.c \
 version.c

LIBDIRS=$(dir $(INCLUDELIBFILES))
load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

TTYAPP=plink
