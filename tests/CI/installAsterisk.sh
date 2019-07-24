#!/usr/bin/env bash

CIDIR=$(dirname $(readlink -fn $0))
UNINSTALL=0
UNINSTALL_ALL=0
source $CIDIR/ci.functions

MAKE=`which make`

if [ x"$DESTDIR" != x ] ; then
	mkdir -p "$DESTDIR"
fi
destdir=${DESTDIR:+DESTDIR=$DESTDIR}

[ $UNINSTALL -gt 0 ] && ${MAKE} ${destdir} uninstall
[ $UNINSTALL_ALL -gt 0 ] && ${MAKE} ${destdir} uninstall-all

${MAKE} ${destdir} install || ${MAKE} ${destdir} NOISY_BUILD=yes install || exit 1
${MAKE} ${destdir} samples
if [ x"$DESTDIR" != x ] ; then
	sed -i -r -e "s@\[directories\]\(!\)@[directories]@g" $DESTDIR/etc/asterisk/asterisk.conf
	sed -i -r -e "s@ /(var|etc|usr)/@ $DESTDIR/\1/@g" $DESTDIR/etc/asterisk/asterisk.conf
fi

set +e
if [ x"$USER_GROUP" != x ] ; then
	chown -R $USER_GROUP $DESTDIR/var/lib/asterisk
	chown -R $USER_GROUP $DESTDIR/var/spool/asterisk
	chown -R $USER_GROUP $DESTDIR/var/log/asterisk
	chown -R $USER_GROUP $DESTDIR/var/run/asterisk
	chown -R $USER_GROUP $DESTDIR/etc/asterisk
fi
ldconfig
