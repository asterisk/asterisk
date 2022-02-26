#!/bin/bash

# codec-install.sh version 2.0
#
# Copyright (C) 2018 James Pearson <jamesp@vicidial.com> LICENSE: AGPLv2
# Copyright (C) 2022 RouteTrst, Inc LICENSE: AGPLv2
#

# Get our variables
CPUNAME="$(cut -d':' -f2 <<<`cat /proc/cpuinfo | grep 'model name' | sed -n 1p`)"
CPUVEN="$(cut -d':' -f2 <<<`cat /proc/cpuinfo | grep 'vendor' | sed -n 1p`)"
CPUFAM="$(cut -d':' -f2 <<<`cat /proc/cpuinfo | grep 'family' | sed -n 1p`)"
CPUFLAG="$(cut -d':' -f2 <<<`cat /proc/cpuinfo | grep 'flag' | sed -n 1p`)"
G729='codec_g729-'
G723='codec_g723-'
CPUARCH='' # Autodetected below, should be blank
OSARCH='gcc4-glibc-x86_64-'
ASTVER='' # Autodetected below, should be blank
SRCDIR='/home/rtsupport/projects/asterisk/third-party/routetrust/codecs/'
MODDIR='/usr/lib/asterisk/modules/'
AST_BIN=/usr/sbin/asterisk

# Debug output
# echo CPU Vendor - $CPUVEN
# echo CPU Family - $CPUFAM
# echo CPU Flags - $CPUFLAG

# Make sure asterisk is even installed before continuing
if [ ! -x $AST_BIN ]; then
        echo "No $AST_BIN found! Is asterisk installed?";
        exit
fi
if [ ! -d $MODDIR ]; then
        echo "No asterisk module directory found at $MODDIR"
        exit
fi

echo

### Make sure we have a supported Asterisk version or exit
RAWASTVER=`$AST_BIN -V`
RAWASTARY=($RAWASTVER)
ASTVERSION=${RAWASTARY[1]}
if [[ $ASTVERSION =~ ^18 ]]; then
        ASTVER="ast180-"
        echo "Found Asterisk 18 - v.$ASTVERSION"
fi

if [ ${#ASTVER} -lt 5 ]; then
        echo "No supported asterisk version found! Asterisk reported as $RAWASTVER"
        exit
fi

### Detect CPU brand/family/model/etc, and build the filename for the codec we need
if [[ "$CPUVEN" == *"AMD"* ]]; then # Handle AMD CPUs
        echo -n "Found AMD CPU, "
        if [ $CPUFAM -ge 10 ]; then
                echo "Barcelona core or better"
                CPUARCH='barcelona.so'
        elif [ $CPUFAM -ge 8 ]; then
                echo -n "Opteron Core, "
                if [[ "$CPUFLAG" == *"sse3"* ]]; then
                        echo "with SSE3 instructions"
                        CPUARCH='opteron-sse3.so'
                else
                        echo "without SSE3 instructions"
                        CPUARCH='opteron.so'
                fi
        else
                echo "but no supported codec found for architecture"
                exit
        fi
elif [[ "$CPUVEN" == *"Intel"* ]]; then # Handle Intel CPUs
        echo -n "Found Intel CPU, "
        if [ $CPUFAM -eq 6 ]; then
                echo -n "Core2 arch or better, "
                if [[ "$CPUFLAG" == *"sse4"* ]]; then
                        echo "with SSE4 instructions"
                        CPUARCH='core2-sse4.so'
                else
                        echo "without SSE4 instructions"
                        CPUARCH='core2.so'
                fi
        elif [[ "$CPUNAME" == *"Pentium(R) 4"* ]]; then
                echo "NetBurst P4 arch"
                CPUARCH="pentium4.so"
        fi
else # Just in case of someone blindly copy-pasta, exit if we aren't a supported CPU
        echo "Could not find compatible Intel or AMD CPU!"
        echo "CPU Found: $CPUNAME"
        exit
fi

echo "G729: $ASTVER$OSARCH$CPUARCH"
echo "G723: $ASTVER$OSARCH$CPUARCH"

echo "Installing from local copies"
if [ -f $SRCDIR$G729$ASTVER$OSARCH$CPUARCH ]; then
        echo "Installing G729 - $G729"
        cp $SRCDIR$G729$ASTVER$OSARCH$CPUARCH $MODDIR/codec_g729.so
else
        echo "Could not find $SRCDIR$G729$ASTVER$OSARCH$CPUARCH to install!"
fi
if [ -f $SRCDIR$G723$ASTVER$OSARCH$CPUARCH ]; then
        echo "Installing G723 - $G723"
        cp $SRCDIR$G723$ASTVER$OSARCH$CPUARCH $MODDIR/codec_g723.so
else
        echo "Could not find $SRCDIR$G723$ASTVER$OSARCH$CPUARCH to install!"
fi

echo
