import os

redirects = [
    ('llvmpipe', 'gallium/drivers/llvmpipe.html'),
    ('postprocess', 'gallium/postprocess.html'),
    ('webmaster', 'https://www.mesa3d.org/website/')
]

def create_redirect(dst):
    tpl = '<html><head><meta http-equiv="refresh" content="0; url={0}"><script>window.location.replace("{0}")</script></head></html>'
    return tpl.format(dst)

def create_redirects(app, docname):
    if not app.builder.name == 'html':
        return
    for src, dst in redirects:
        path = os.path.join(app.outdir, '{0}.html'.format(src))
        with open(path, 'w') as f:
            f.write(create_redirect(dst))

def setup(app):
    app.connect('build-finished', create_redirects)
