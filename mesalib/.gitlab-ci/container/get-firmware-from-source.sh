#!/usr/bin/env bash
# shellcheck disable=SC2046 # we want to arg-split FIRMWARE_FILES
# shellcheck disable=SC2086 # as above
# shellcheck disable=SC2116 # as above

set -e

ROOTFS=$1
FIRMWARE_FILES=$2

if [ -n "${FIRMWARE_FILES:-}" ]; then
  FIRMWARE=$(jq -s '.' $(echo "$FIRMWARE_FILES"))
else
  FIRMWARE=""
fi

if ! echo "$FIRMWARE" | jq empty; then
  echo "FIRMWARE contains invalid JSON."
fi

for item in $(echo "$FIRMWARE" | jq -c '.[]'); do
  src=$(echo "$item" | jq -r '.src')
  git_hash=$(echo "$item" | jq -r '.git_hash')
  dst=$(echo "$item" | jq -r '.dst')

  if [ "$src" = "null" ] || [ "$dst" = "null"  ]; then
    echo "Missing src or dst for $item."
    continue
  fi

  # Remove any trailing slashes from src and dst
  src=${src%/}
  dst=${dst%/}

  # Remove any leading slash
  dst=${dst#/}

  if [ "$(echo "$item" | jq '.files | length')" -eq 0 ]; then
    echo "No files specified for $item."
    continue
  fi

  for file in $(echo "$item" | jq -r '.files[]'); do
    FIRMWARE_SRC_PATH="${src}/${file}"
    if [ "$git_hash" != "null" ]; then
      FIRMWARE_SRC_PATH="${FIRMWARE_SRC_PATH}?h=${git_hash}"
    fi
    FIRMWARE_DST_DIR="${ROOTFS}/${dst}"

    curl -L --retry 4 -f --retry-all-errors --retry-delay 60 --create-dirs --output-dir "${FIRMWARE_DST_DIR}" -o "${file}" "${FIRMWARE_SRC_PATH}"
  done

done
