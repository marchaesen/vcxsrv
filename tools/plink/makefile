ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1 on the command line or as environment variable)
endif

DEFINES += SECURITY_WIN32

CSRCS = winplink.c winhandl.c misc.c settings.c winstore.c windefs.c winmisc.c wincons.c \
        logging.c winnet.c tree234.c winnoise.c sshrand.c cmdline.c sshsha.c timing.c \
        be_all.c rlogin.c proxy.c winproxy.c cproxy.c sshmd5.c time.c version.c ssh.c \
        sshdh.c sshzlib.c sshbn.c sshrsa.c sshcrcda.c sshpubk.c sshdes.c wingss.c \
        sshblowf.c sshsh512.c sshsh256.c sshaes.c pinger.c ssharcf.c x11fwd.c winpgntc.c \
        winx11.c portfwd.c sshcrc.c wildcard.c ldisc.c sshdss.c raw.c telnet.c sshgssc.c \
        pgssapi.c winnojmp.c conf.c

TTYAPP=plink
