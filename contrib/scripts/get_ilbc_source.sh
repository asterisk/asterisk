#!/bin/sh -e

echo "***"
echo "This script will download the Global IP Solutions iLBC encoder/decoder"
echo "source code from http://ilbcfreeware.org. Use of this code requires"
echo "agreeing to the license agreement present at that site."
echo ""
echo "This script assumes that you have already agreed to the license agreement."
echo "If you have not done so, you can abort the script now."
echo "***"

read

wget -P codecs/ilbc http://www.ietf.org/rfc/rfc3951.txt

wget -P codecs/ilbc http://www.ilbcfreeware.org/documentation/extract-cfile.awk

(cd codecs/ilbc && awk -f extract-cfile.awk rfc3951.txt)

echo "***"
echo "The iLBC source code download is complete."
echo "***"
