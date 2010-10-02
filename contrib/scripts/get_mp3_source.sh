#!/bin/sh -e

if [ -f addons/mp3/mpg123.h ]; then
    echo "***"
    echo "The MP3 source code appears to already be present and does not"
    echo "need to be downloaded."
    echo "***"

    exit 1
fi

svn export http://svn.digium.com/svn/thirdparty/mp3/trunk addons/mp3 $@

exit 0
