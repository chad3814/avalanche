#!/bin/bash

I=$1
shift
DIR=$(dirname `which ffmpeg`)
if [ "$DIR" == "/usr/bin" -o "$DIR" == "/usr/local/bin" ]; then
    exit
fi

if [ "$I" == "-i" ]; then
    echo "$(dirname $DIR)/include"
else
    echo "-L$(dirname $DIR)/lib"
fi
