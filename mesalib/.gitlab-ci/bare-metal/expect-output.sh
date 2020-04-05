#!/bin/bash

set -e

echo "Waiting for $1 to say '$2'"

while ! grep -q "$2" $1; do
  sleep 2
done
