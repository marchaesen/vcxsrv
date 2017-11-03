xcb_dep = dependency('xcb', required: false)

if get_option('xvfb')
    if xcb_dep.found()
        requestlength = executable('request-length', 'request-length.c', dependencies: xcb_dep)
        test('request-length', simple_xinit, args: [requestlength, '--', xvfb_server])
    endif
endif
