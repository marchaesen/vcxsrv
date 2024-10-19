get_option('html-docs').require(
  run_command(prog_python, '-c',
  '''
try:
  from packaging.version import Version
except:
  from distutils.version import StrictVersion as Version
import hawkmoth
assert Version(hawkmoth.__version__) >= Version("0.16.0")
  ''', check: false).returncode() == 0,
  error_message: 'hawkmoth >= 0.16.0 required'
)

html_docs = custom_target(
  'html',
  depfile: 'docs.d',
  depend_files: files('conf.py'),
  command: [sphinx, '-b', 'html', '-q',
            '-Ddepfile=@DEPFILE@',
            meson.current_source_dir(), '@OUTPUT@'],
  output: 'html',
  build_by_default: get_option('html-docs').enabled(),
)

html_docs_path = get_option('html-docs-path')
if html_docs_path == ''
  html_docs_path = join_paths(get_option('datadir'), 'doc', 'mesa')
endif

install_subdir(html_docs.full_path(),
  install_dir: html_docs_path,
  exclude_files: ['.buildinfo'],
  exclude_directories: ['.doctrees'],
  strip_directory: true
)
