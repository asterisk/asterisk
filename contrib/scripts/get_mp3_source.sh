#!/bin/sh -e

if [ -f addons/mp3/mpg123.h ]; then
    echo "***"
    echo "The MP3 source code appears to already be present and does not"
    echo "need to be downloaded."
    echo "***"

    # Manually patch interface.c if not done yet.
    if ! grep -q WRAP_LIBC_MALLOC addons/mp3/interface.c; then
        sed -i -e '/#include "asterisk.h"/i#define WRAP_LIBC_MALLOC' \
            addons/mp3/interface.c
    fi

    exit 1
fi

svn export http://svn.digium.com/svn/thirdparty/mp3/trunk addons/mp3 $@

# Manually patch interface.c if not done yet.
if ! grep -q WRAP_LIBC_MALLOC addons/mp3/interface.c; then
    sed -i -e '/#include "asterisk.h"/i#define WRAP_LIBC_MALLOC' \
        addons/mp3/interface.c
fi

exit 0
