#! /bin/bash

set -e

commit=$1

echo git clang-format --diff "$commit"
git clang-format --diff "$commit"
