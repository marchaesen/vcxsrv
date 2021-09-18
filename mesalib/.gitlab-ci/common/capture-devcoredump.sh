#!/bin/sh

while true; do
  devcds=`find /sys/devices/virtual/devcoredump/ -name data 2>/dev/null`
  for i in $devcds; do
    echo "Found a devcoredump at $i."
    if cp $i /results/first.devcore; then
      echo 1 > $i
      echo "Saved to the job artifacts at /first.devcore"
      exit 0
    fi
  done
  sleep 10
done
