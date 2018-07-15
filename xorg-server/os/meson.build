srcs_os = [
    'WaitFor.c',
    'access.c',
    'auth.c',
    'backtrace.c',
    'client.c',
    'connection.c',
    'inputthread.c',
    'io.c',
    'mitauth.c',
    'oscolor.c',
    'osinit.c',
    'ospoll.c',
    'utils.c',
    'xdmauth.c',
    'xsha1.c',
    'xstrans.c',
    'xprintf.c',
    'log.c',
]

# Wrapper code for missing C library functions
srcs_libc = []
if not conf_data.get('HAVE_REALLOCARRAY')
    srcs_libc += 'reallocarray.c'
endif
if not conf_data.get('HAVE_STRCASECMP')
    srcs_libc += 'strcasecmp.c'
endif
if not conf_data.get('HAVE_STRCASESTR')
    srcs_libc += 'strcasestr.c'
endif
if not conf_data.get('HAVE_STRLCAT')
    srcs_libc += 'strlcat.c'
endif
if not conf_data.get('HAVE_STRLCPY')
    srcs_libc += 'strlcpy.c'
endif
if not conf_data.get('HAVE_STRNDUP')
    srcs_libc += 'strndup.c'
endif
if not conf_data.get('HAVE_TIMINGSAFE_MEMCMP')
    srcs_libc += 'timingsafe_memcmp.c'
endif
if not conf_data.get('HAVE_POLL')
    srcs_os += 'xserver_poll.c'
endif

if conf_data.get('BUSFAULT')
    srcs_os += 'busfault.c'
endif

if get_option('xdmcp')
    srcs_os += 'xdmcp.c'
endif

rpc_dep = []
if get_option('secure-rpc')
    # prefer libtirpc (if available), otherwise ensure RPC functions are
    # provided by libc.
    rpc_dep = dependency('libtirpc', required: false)
    if not (rpc_dep.found() or cc.has_header('rpc/rpc.h'))
        error('secure-rpc requested, but neither libtirpc or libc RPC support were found')
    endif

    srcs_os += 'rpcauth.c'
endif

libxlibc = []
if srcs_libc.length() > 0
    libxlibc = static_library('libxlibc',
        srcs_libc,
        include_directories: inc,
        dependencies: [
            xproto_dep,
        ],
    )
endif

libxserver_os = static_library('libxserver_os',
    srcs_os,
    include_directories: inc,
    dependencies: [
        common_dep,
        dl_dep,
        sha1_dep,
        rpc_dep,
        dependency('xau')
    ],
    link_with: libxlibc,
)
