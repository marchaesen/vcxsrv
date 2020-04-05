ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1 on the command line or as environment variable)
endif

DEFINES += SECURITY_WIN32

CSRCS = winplink.c winhandl.c misc.c settings.c winstore.c windefs.c winmisc.c wincons.c \
        logging.c winnet.c tree234.c winnoise.c sshrand.c cmdline.c sshsha.c timing.c \
        be_all.c rlogin.c proxy.c winproxy.c cproxy.c sshmd5.c time.c version.c ssh.c \
        sshdh.c sshzlib.c sshrsa.c sshcrcda.c sshpubk.c sshdes.c wingss.c \
        sshblowf.c sshsh512.c sshsh256.c sshaes.c pinger.c ssharcf.c x11fwd.c winpgntc.c \
        winx11.c portfwd.c sshcrc.c wildcard.c ldisc.c sshdss.c raw.c telnet.c sshgssc.c \
        pgssapi.c winnojmp.c conf.c callback.c sshshare.c winsecur.c errsock.c winshare.c \
        winnps.c winnpc.c noterm.c winhsock.c sshecc.c sshccp.c be_misc.c wincapi.c miscucs.c \
				winucs.c sessprep.c marshal.c nullplug.c ssh1bpp.c ssh2bpp.c ssh1censor.c ssh2censor.c \
				ssh2bpp-bare.c sshcommon.c sshverstring.c sshmac.c agentf.c ssh1login.c ssh1connection.c \
				ssh2transport.c ssh2userauth.c ssh2connection.c ssh1connection-client.c ssh2transhk.c \
				ssh2connection-client.c mainchan.c ssh2kex-client.c memory.c ecc.c mpint.c utils.c \
				winmiscs.c sshauxcrypt.c sshhmac.c sshprng.c stripctrl.c wcwidth.c winnohlp.c \
				winselcli.c sshutils.c wincliloop.c clicons.c supdup.c sshsha3.c

TTYAPP=plink
