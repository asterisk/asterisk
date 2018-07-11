#!/bin/bash
MAKE=`which make`
if [ x"${@}" != x ] ; then
	mkdir -p "${@}"
fi
destdir=${@:+DESTDIR=${@}}

${MAKE} ${destdir} install || ${MAKE} ${destdir} NOISY_BUILD=yes install || exit 1
${MAKE} ${destdir} samples
if [ -n "${@}" ] ; then
	sed -i -r -e "s@\[directories\]\(!\)@[directories]@g" $@/etc/asterisk/asterisk.conf
	sed -i -r -e "s@ /(var|etc|usr)/@ ${@}/\1/@g" $@/etc/asterisk/asterisk.conf
fi

set +e
chown -R jenkins:users ${@}/var/lib/asterisk
chown -R jenkins:users ${@}/var/spool/asterisk
chown -R jenkins:users ${@}/var/log/asterisk
chown -R jenkins:users ${@}/var/run/asterisk
chown -R jenkins:users ${@}/etc/asterisk
[ ! -d ${@}/tmp/asterisk-jenkins ] && mkdir ${@}/tmp/asterisk-jenkins
chown -R jenkins:users ${@}/tmp/asterisk-jenkins
ldconfig