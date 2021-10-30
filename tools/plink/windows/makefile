LIBRARY=libwindows

ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1 on the command line or as environment variable)
endif

vpath %.c utils

DEFINES += SECURITY_WIN32
INCLUDES := .. $(INCLUDES) ../terminal

UTILS_CSRCS = arm_arch_queries.c \
 cryptoapi.c \
 defaults.c \
 dll_hijacking_protection.c \
 dputs.c \
 escape_registry_key.c \
 filename.c \
 fontspec.c \
 get_system_dir.c \
 get_username.c \
 getdlgitemtext_alloc.c \
 gss.c \
 is_console_handle.c \
 load_system32_dll.c \
 ltime.c \
 makedlgitemborderless.c \
 message_box.c \
 minefield.c \
 open_for_write_would_lose_data.c \
 pgp_fingerprints_msgbox.c \
 platform_get_x_display.c \
 registry_get_string.c \
 request_file.c \
 security.c \
 split_into_argv.c \
 version.c \
 win_strerror.c

CSRCS =  agent-client.c \
 cliloop.c \
 config.c \
 console.c \
 handle-io.c \
 handle-socket.c \
 handle-wait.c \
 help.c \
 local-proxy.c \
 named-pipe-client.c \
 named-pipe-server.c \
 network.c \
 nohelp.c \
 noise.c \
 no-jump-list.c \
 plink.c \
 printing.c \
 putty.c \
 select-cli.c \
 serial.c \
 sharing.c \
 sizetip.c \
 storage.c \
 unicode.c \
 x11.c

CSRCS += $(UTILS_CSRCS)

