LIBRARY=libotherbackends

ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1 on the command line or as environment variable)
endif

DEFINES += SECURITY_WIN32
INCLUDES := .. ../windows $(INCLUDES)

CSRCS = raw.c \
 rlogin.c \
 supdup.c \
 telnet.c \
 testback.c

