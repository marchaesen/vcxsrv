srcs_composite = [
	'compalloc.c',
	'compext.c',
	'compinit.c',
	'compoverlay.c',
	'compwindow.c',
]

hdrs_composite = [
	'compositeext.h',
]

libxserver_composite = static_library('libxserver_composite',
	srcs_composite,
	include_directories: inc,
	dependencies: common_dep,
)

install_data(hdrs_composite, install_dir: xorgsdkdir)
