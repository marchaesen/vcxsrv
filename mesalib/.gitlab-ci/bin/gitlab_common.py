#!/usr/bin/env python3
# Copyright © 2020 - 2022 Collabora Ltd.
# Authors:
#   Tomeu Vizoso <tomeu.vizoso@collabora.com>
#   David Heidelberg <david.heidelberg@collabora.com>
#
# SPDX-License-Identifier: MIT
'''Shared functions between the scripts.'''

import os
import time
from typing import Optional


def get_gitlab_project(glab, name: str):
    """Finds a specified gitlab project for given user"""
    glab.auth()
    username = glab.user.username
    return glab.projects.get(f"{username}/mesa")


def read_token(token_arg: Optional[str]) -> str:
    """pick token from args or file"""
    if token_arg:
        return token_arg
    return (
        open(os.path.expanduser("~/.config/gitlab-token"), encoding="utf-8")
        .readline()
        .rstrip()
    )


def wait_for_pipeline(project, sha: str):
    """await until pipeline appears in Gitlab"""
    print("⏲ for the pipeline to appear..", end="")
    while True:
        pipelines = project.pipelines.list(sha=sha)
        if pipelines:
            print("", flush=True)
            return pipelines[0]
        print("", end=".", flush=True)
        time.sleep(1)
