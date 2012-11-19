#!/bin/sh

grep 'name\[Group1\]' * | sed 's/[[:space:]]*name\[Group1\].*=[[:space:]]*//;s/;[[:space:]]*$//' | sort
