#!/usr/bin/env bash

# When changing this file, check if the *_BUIL_TAG tags in
# .gitlab-ci/image-tags.yml need updating.

set -eu

# Early check for required env variables, relies on `set -u`
: "$S3_JWT_FILE_SCRIPT"

if [ -z "$1" ]; then
  echo "usage: $(basename "$0") <CONTAINER_CI_JOB_NAME>" 1>&2
  exit 1
fi

CONTAINER_CI_JOB_NAME="$1"

# Tasks to perform before executing the script of a container job
eval "$S3_JWT_FILE_SCRIPT"
unset S3_JWT_FILE_SCRIPT

trap 'rm -f ${S3_JWT_FILE}' EXIT INT TERM

bash ".gitlab-ci/container/${CONTAINER_CI_JOB_NAME}.sh"
