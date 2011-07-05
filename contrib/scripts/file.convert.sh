#/bin/bash

# Script written by Trey Blancher (support@digium.com)

# This script is designed to convert all files of type $SRC to
# the $DST format, for the given $LANGUAGE.  It traverses the given
# language directory (by default in /var/lib/asterisk/sounds/), and
# converts each file with filename extension $SRC, and converts them
# using Asterisk to files with type and extension $DST.

LANGUAGE=en    # change accordingly, if converting custom sounds you may want to omit this variable
SRC=gsm   # change accordingly (e.g. to wav, etc.)
DST=g729   # change accordingly (e.g. to wav, etc.)
SOUNDS=/var/lib/asterisk/sounds  # for custom sounds change this directory to your custom sound directory

for file in $(find ${SOUNDS}/${LANGUAGE}/ -depth -type f  -name *.${SRC});
do
   #echo $file
   asterisk -rx "file convert $file $(dirname $file)/$(basename $file $SRC)$DST"
done
