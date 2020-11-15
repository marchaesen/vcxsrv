import os
import pathlib
from urllib.parse import urlparse

def create_redirect(dst):
    tpl = '<html><head><meta http-equiv="refresh" content="0; url={0}"><script>window.location.replace("{0}")</script></head></html>'
    return tpl.format(dst)

def create_redirects(app, docname):
    if not app.builder.name == 'html':
        return
    for src, dst in app.config.html_redirects:
        path = os.path.join(app.outdir, '{0}.html'.format(src))

        os.makedirs(os.path.dirname(path), exist_ok=True)

        if urlparse(dst).scheme == "":
            dst = pathlib.posixpath.relpath(dst, start=os.path.dirname(src))
            if not os.path.isfile(os.path.join(os.path.dirname(path), dst)):
                raise Exception('{0} does not exitst'.format(dst))

        with open(path, 'w') as f:
            f.write(create_redirect(dst))

def setup(app):
    app.add_config_value('html_redirects', [], '')
    app.connect('build-finished', create_redirects)
