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
 interactor.c \
 socks4.c \
 socks5.c \
 http.c \
 telnet.c \
 local.c \
 be_list.c \
 sshproxy.c \
 sshpubk.c \
 cproxy.c \
 pinger.c \
 ldisc.c \
 callback.c \
 errsock.c \
 null-plug.c \
 clicons.c \
 console.c \
 x11disp.c \
 version.c \
 null-seat.c \
 null-opener.c \
 null-key.c \
 null-cipher.c \
 null-mac.c \
 null-lp.c \
 bidi.c \
 no-lineedit.c \
 no-term.c \
 null-socket.c

vpath %.c proxy stubs terminal

LIBDIRS=$(dir $(INCLUDELIBFILES))
load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

TTYAPP=plink
