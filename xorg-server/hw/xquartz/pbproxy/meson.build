build_standalone_pbproxy = get_option('xpbproxy')

pbproxy_defs = [bundle_id_def]
if build_standalone_pbproxy
    pbproxy_defs += ['-DSTANDALONE_XPBPROXY']
endif

libapplewm_dep = dependency('applewm', version: '>=1.4')

libxpbproxy = static_library('xpbproxy',
    ['main.m',
     'x-input.m',
     'x-selection.m'],
    dependencies: [applewmproto_dep, libapplewm_dep, dependency('xfixes'), dependency('x11')],
    objc_args: pbproxy_defs,
)

cocoa = dependency('Cocoa', method: 'extraframework')

# standalone xpbproxy
if build_standalone_pbproxy
    executable('xpbproxy',
        'app-main.m',
        link_with: libxpbproxy,
        dependencies: [cocoa, dependency('x11')],
        objc_args: pbproxy_defs,
        install: true,
    )
endif
