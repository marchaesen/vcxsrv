#!/bin/sh

# run git from the sources directory
cd "$(dirname "$0")"

# don't print anything if git fails
if ! git_sha1=$(git --git-dir=../.git rev-parse --short=10 HEAD 2>/dev/null)
then
  exit
fi

printf '#define MESA_GIT_SHA1 "git-%s"\n' "$git_sha1"
