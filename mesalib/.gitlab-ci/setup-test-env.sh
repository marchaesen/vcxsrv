#!/usr/bin/env bash
# shellcheck disable=SC2048
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC2155 # mktemp usually not failing

shopt -s expand_aliases

function _x_store_state {
    if [[ "$-" == *"x"* ]]; then
      previous_state_x=1
    else
      previous_state_x=0
    fi
}
_x_store_state
alias x_store_state='{ _x_store_state; } >/dev/null 2>/dev/null'

function _x_off {
    x_store_state
    set +x
}
alias x_off='{ _x_off; } >/dev/null 2>/dev/null'

function _x_restore {
  [ $previous_state_x -eq 0 ] || set -x
}
alias x_restore='{ _x_restore; } >/dev/null 2>/dev/null'

export JOB_START_S=$(date -u +"%s" -d "${CI_JOB_STARTED_AT:?}")

function get_current_minsec {
    DATE_S=$(date -u +"%s")
    CURR_TIME=$((DATE_S-JOB_START_S))
    printf "%02d:%02d" $((CURR_TIME/60)) $((CURR_TIME%60))
}

function _build_section_start {
    local section_params=$1
    shift
    local section_name=$1
    CURRENT_SECTION=$section_name
    shift
    CYAN="\e[0;36m"
    ENDCOLOR="\e[0m"

    CURR_MINSEC=$(get_current_minsec)
    echo -e "\n\e[0Ksection_start:$(date +%s):$section_name$section_params\r\e[0K${CYAN}[${CURR_MINSEC}] $*${ENDCOLOR}\n"
    x_restore
}
alias build_section_start="x_off; _build_section_start"

function _section_start {
    build_section_start "[collapsed=true]" $*
    x_restore
}
alias section_start="x_off; _section_start"

function _uncollapsed_section_start {
    build_section_start "" $*
    x_restore
}
alias uncollapsed_section_start="x_off; _uncollapsed_section_start"

function _build_section_end {
    echo -e "\e[0Ksection_end:$(date +%s):$1\r\e[0K"
    CURRENT_SECTION=""
    x_restore
}
alias build_section_end="x_off; _build_section_end"

function _section_end {
    build_section_end $*
    x_restore
}
alias section_end="x_off; _section_end"

function _section_switch {
    if [ -n "$CURRENT_SECTION" ]
    then
        build_section_end $CURRENT_SECTION
        x_off
    fi
    build_section_start "[collapsed=true]" $*
    x_restore
}
alias section_switch="x_off; _section_switch"

function _uncollapsed_section_switch {
    if [ -n "$CURRENT_SECTION" ]
    then
        build_section_end $CURRENT_SECTION
        x_off
    fi
    build_section_start "" $*
    x_restore
}
alias uncollapsed_section_switch="x_off; _uncollapsed_section_switch"

export -f _x_store_state
export -f _x_off
export -f _x_restore
export -f get_current_minsec
export -f _build_section_start
export -f _section_start
export -f _build_section_end
export -f _section_end
export -f _section_switch
export -f _uncollapsed_section_switch

# Freedesktop requirement (needed for Wayland)
[ -n "${XDG_RUNTIME_DIR:-}" ] || export XDG_RUNTIME_DIR="$(mktemp -p "$PWD" -d xdg-runtime-XXXXXX)"

if [ -z "${RESULTS_DIR:-}" ]; then
	export RESULTS_DIR="${PWD%/}/results"
	if [ -e "${RESULTS_DIR}" ]; then
		rm -rf "${RESULTS_DIR}"
	fi
	mkdir -p "${RESULTS_DIR}"
fi

function error {
    RED="\e[0;31m"
    ENDCOLOR="\e[0m"
    # we force the following to be not in a section
    if [ -n "${CURRENT_SECTION:-}" ]; then
      section_end $CURRENT_SECTION
      x_off
    fi

    CURR_MINSEC=$(get_current_minsec)
    echo -e "\n${RED}[${CURR_MINSEC}] ERROR: $*${ENDCOLOR}\n"
    x_restore
}

function trap_err {
    x_off
    error ${CURRENT_SECTION:-'unknown-section'}: ret code: $*
}

export -f error
export -f trap_err

set -E
trap 'trap_err $?' ERR
