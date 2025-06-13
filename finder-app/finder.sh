#!/bin/bash

if [ $# -lt 2 ]; then
	echo invalid args >&2
	exit 1
fi

FILESDIR=$1
SEARCHSTR=$2

if [ ! -d "$FILESDIR" ]; then
	echo "$FILESDIR" is not a valid directory >&2
	exit 1
fi

FILES=$(find "$FILESDIR" -type f)
COUNT_FILES=$(echo "$FILES" | wc -l)
COUNT_LINES=$(grep "$SEARCHSTR" $FILES | wc -l)

echo The number of files are $COUNT_FILES and the number of matching lines are $COUNT_LINES
