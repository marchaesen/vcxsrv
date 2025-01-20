#!/usr/bin/env bash

SCRIPTS_DIR="$(realpath "$(dirname "$0")")"

is_bash() {
    [[ $1 == *.sh ]] && return 0
    [[ $1 == */bash-completion/* ]] && return 0
    [[ $(file -b --mime-type "$1") == text/x-shellscript ]] && return 0
    return 1
}

anyfailed=0

while IFS= read -r -d $'' file; do
    if is_bash "$file" ; then
        if ! shellcheck "$file"; then
            anyfailed=1
        fi
    fi
done < <(find "$SCRIPTS_DIR" -type f \! -path "./.git/*" -print0)

exit "$anyfailed"
