LIBRARY=libwindows

ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1 on the command line or as environment variable)
endif

vpath %.c utils

DEFINES += SECURITY_WIN32
INCLUDES := .. $(INCLUDES) ../terminal

UTILS_CSRCS = \
  agent_mutex_name.c \
  agent_named_pipe_name.c \
  arm_arch_queries.c \
  aux_match_opt.c \
  centre_window.c \
  cryptoapi.c \
  defaults.c \
  dll_hijacking_protection.c \
  dputs.c \
  escape_registry_key.c \
  filename.c \
  fontspec.c \
  getdlgitemtext_alloc.c \
  get_system_dir.c \
  get_username.c \
  gui-timing.c \
  interprocess_mutex.c \
  is_console_handle.c \
  load_system32_dll.c \
  ltime.c \
  makedlgitemborderless.c \
  make_spr_sw_abort_winerror.c \
  message_box.c \
  minefield.c \
  open_for_write_would_lose_data.c \
  pgp_fingerprints_msgbox.c \
  platform_get_x_display.c \
  registry.c \
  request_file.c \
  screenshot.c \
  security.c \
  shinydialogbox.c \
  split_into_argv.c \
  version.c \
  win_strerror.c \
	cmdline_arg.c \
	split_into_argv_w.c

CSRCS =  agent-client.c \
 cliloop.c \
 handle-wait.c \
 select-cli.c \
 nohelp.c \
 console.c \
 storage.c \
 network.c \
 handle-socket.c \
 named-pipe-client.c \
 named-pipe-server.c \
 local-proxy.c \
 x11.c \
 noise.c \
 gss.c \
 sharing.c \
 serial.c \
 no-jump-list.c \
 config.c \
 handle-io.c \
 plink.c \
 printing.c \
 sizetip.c \
 unicode.c \

CSRCS += $(UTILS_CSRCS)

