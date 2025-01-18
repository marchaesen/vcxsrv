#!/usr/bin/env bash
# shellcheck disable=SC1091  # the path is created in build-kdl and
# here is check if exist
# shellcheck disable=SC2086 # we want the arguments to be expanded

if ! [ -f /ci-kdl/bin/activate ]; then
  echo -e "ci-kdl not installed; not monitoring temperature"
  exit 0
fi

KDL_ARGS="
	--output-file=${RESULTS_DIR}/kdl.json
	--log-level=WARNING
	--num-samples=-1
"

source /ci-kdl/bin/activate
exec /ci-kdl/bin/ci-kdl ${KDL_ARGS}
