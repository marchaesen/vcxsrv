LIBRARY=libssh

ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1 on the command line or as environment variable)
endif

DEFINES += SECURITY_WIN32
INCLUDES := .. ../windows $(INCLUDES)

CSRCS = agentf.c \
 bpp1.c \
 bpp2.c \
 bpp-bare.c \
 censor1.c \
 censor2.c \
 common.c \
 connection1.c \
 connection1-client.c \
 connection2.c \
 connection2-client.c \
 crc-attack-detector.c \
 gssc.c \
 kex2-client.c \
 login1.c \
 mainchan.c \
 nosharing.c \
 pgssapi.c \
 portfwd.c \
 sesschan.c \
 sharing.c \
 ssh.c \
 transient-hostkey-cache.c \
 transport2.c \
 userauth2-client.c \
 verstring.c \
 x11fwd.c \
 zlib.c
