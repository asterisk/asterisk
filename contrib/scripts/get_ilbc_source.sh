#!/bin/sh -e

if [ -f codecs/ilbc/iLBC_define.h ]; then
    echo "***"
    echo "The iLBC source code appears to already be present and does not"
    echo "need to be downloaded."
    echo "***"

    exit 1
fi

echo "***"
echo "This script will download and extract the iLBC RFC from http://ilbcfreeware.org."
echo "As Google acquired Global IP Solutions, usage of iLBC is bound by the same"
echo "license as the WebRTC project (http://www.webrtc.org).  Usage of the iLBC codec"
echo "entails agreeing to the license agreement present at that site."
echo ""
echo "This script assumes that you have already agreed to the license agreement."
echo "If you have not done so, you can abort the script now."
echo "***"

read tmp

wget -P codecs/ilbc http://www.ietf.org/rfc/rfc3951.txt

wget -q -O - http://www.ilbcfreeware.org/documentation/extract-cfile.txt | tr -d '\r' > codecs/ilbc/extract-cfile.awk

(cd codecs/ilbc && awk -f extract-cfile.awk rfc3951.txt)

echo "***"
echo "The iLBC source code download is complete."
echo "***"

exit 0
