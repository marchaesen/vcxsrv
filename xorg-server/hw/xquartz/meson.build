add_languages('objc')

apple_applications_dir = get_option('apple-applications-dir')
apple_application_name = get_option('apple-application-name')
bundle_id_prefix = get_option('bundle-id-prefix')
xquartz_sparkle_feed_url = get_option('sparkle-feed-url')

bundle_version_string = meson.project_version() # CFBundleShortVersionString
bundle_version = release                        # CFBundleVersion

bundle_id_def = '-DBUNDLE_ID_PREFIX="@0@"'.format(bundle_id_prefix)

bundle_root = join_paths(apple_applications_dir, apple_application_name + '.app')

# using sparkle update framework?
build_sparkle = xquartz_sparkle_feed_url != ''
if build_sparkle
    sparkle = dependency('Sparkle', method: 'extraframework')
endif

# libxquartz
srcs_libxquartz = [
    'X11Application.m',
    'X11Controller.m',
    'applewm.c',
    'darwin.c',
    'darwinEvents.c',
    'darwinXinput.c',
    'keysym2ucs.c',
    'quartz.c',
    'quartzCocoa.m',
    'quartzKeyboard.c',
    'quartzStartup.c',
    'quartzRandR.c',
    'console_redirect.c',
    '../../mi/miinitext.c',
]

libxquartz_defs = [
    '-DINXQUARTZ',
    '-DUSE_NEW_CLUT',
    '-DX11LIBDIR="@0@"'.format(join_paths(get_option('prefix'),
                               get_option('libdir'))),
    '-DXFree86Server',
    '-DXQUARTZ',
    '-DXSERVER_VERSION="@0@"'.format(meson.project_version()),
    bundle_id_def,
]

if cc.has_function('dispatch_async')
    libxquartz_defs += '-DHAVE_LIBDISPATCH'
endif

libXquartz = static_library('Xquartz',
    srcs_libxquartz,
    c_args: libxquartz_defs,
    objc_args: libxquartz_defs,
    include_directories: [inc, '../../pseudoramiX', '../../miext/rootless', top_dir_inc],
    dependencies: [xproto_dep, pixman_dep],
    link_with: [libxserver_pseudoramix, libxserver_miext_rootless],
)

subdir('bundle')
if build_glx
    subdir('GL')
endif
subdir('pbproxy')
subdir('xpr')
subdir('mach-startup')

# manpage
xquartz_man = configure_file(
    input: 'man/Xquartz.man',
    output: 'Xquartz.1',
    configuration: manpage_config,
)
install_man(xquartz_man)
