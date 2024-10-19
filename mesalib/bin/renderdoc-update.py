#!/usr/bin/env python3

import base64
import pathlib
import requests
import subprocess

def error(msg: str) -> None:
    print('\033[31m' + msg + '\033[0m')

if __name__ == '__main__':
    git_toplevel = subprocess.check_output(['git', 'rev-parse', '--show-toplevel'],
                                           stderr=subprocess.DEVNULL).decode("ascii").strip()
    if not pathlib.Path(git_toplevel).resolve() == pathlib.Path('.').resolve():
        error('Please run this script from the root folder ({})'.format(git_toplevel))
        exit(1)

    file = 'include/renderdoc_app.h'
    url = 'https://raw.githubusercontent.com/baldurk/renderdoc/v1.1/renderdoc/api/app/renderdoc_app.h'

    print('Syncing {}...'.format(file), end=' ', flush=True)
    req = requests.get(url)

    if not req.ok:
        error('Failed to retrieve file: {} {}'.format(req.status_code, req.reason))
        exit(1)

    with open(file, 'wb') as f:
        f.write(req.content)

    print('Done')
