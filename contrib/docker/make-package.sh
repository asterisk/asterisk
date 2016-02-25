#!/bin/bash
# This script intended to be run from the packager container. Please see the
# README.md file for more information on how this script is used.
#
set -ex
[ -n "$1" ]
mkdir -p /opt

# move into the application directory where Asterisk source exists
cd /application

# strip the source of any Git-isms
rsync -av --exclude='.git' . /tmp/application

# move to the build directory and build Asterisk
cd /tmp/application
./configure
cd menuselect
make menuselect
cd ..
make menuselect-tree

menuselect/menuselect --check-deps menuselect.makeopts

# Do not include sound files. You should be mounting these from and external
# volume.
sed -i -e 's/MENUSELECT_MOH=.*$/MENUSELECT_MOH=/' menuselect.makeopts
sed -i -e 's/MENUSELECT_CORE_SOUNDS=.*$/MENUSELECT_CORE_SOUNDS=/' menuselect.makeopts

# Build it!
make all install DESTDIR=/tmp/installdir

rm -rf /tmp/application
cd /build

# Use the Fine Package Management system to build us an RPM without all that
# reeking effort.
fpm -t rpm -s dir -n asterisk-custom --version "$1" \
    --depends libedit \
    --depends libxslt \
    --depends jansson \
    --depends pjproject \
    --depends openssl \
    --depends libxml2 \
    --depends unixODBC \
    --depends libcurl \
    --depends libogg \
    --depends libvorbis \
    --depends speex \
    --depends spandsp \
    --depends freetds \
    --depends net-snmp \
    --depends iksemel \
    --depends corosynclib \
    --depends newt \
    --depends lua \
    --depends sqlite \
    --depends freetds \
    --depends radiusclient-ng \
    --depends postgresql \
    --depends neon \
    --depends libical \
    --depends openldap \
    --depends sqlite2 \
    --depends mysql \
    --depends bluez \
    --depends gsm \
    --depends libuuid \
    --depends libsrtp \
    -C /tmp/installdir etc usr var

chown -R --reference /application/contrib/docker/make-package.sh .
