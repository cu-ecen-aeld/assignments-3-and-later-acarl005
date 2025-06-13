#!/bin/bash

if [ $# -lt 2 ]; then
	echo invalid args >&2
	exit 1
fi

WRITEFILE=$1
WRITESTR=$2

DIRNAME=$(dirname "$WRITEFILE")
mkdir -p "$DIRNAME"

if [ $? -ne 0 ]; then
	echo unable to create parent dir $DIRNAME >&2
	exit 1
fi

echo "$WRITESTR" > "$WRITEFILE"

if [ ! -f "$WRITEFILE" ]; then
	echo failed to create file $WRITEFILE >&2
	exit 1
fi
