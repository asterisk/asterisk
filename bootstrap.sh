#!/bin/sh

check_for_app() {
	$1 --version 2>&1 >/dev/null
	if [ $? != 0 ]
	then
		echo "Please install $1 and run bootstrap.sh again!"
		exit 1
	fi
}

# OpenBSD: pkg_add autoconf%2.63 automake%1.9 metaauto
test -n "$AUTOCONF_VERSION" || export AUTOCONF_VERSION=2.63
test -n "$AUTOMAKE_VERSION" || export AUTOMAKE_VERSION=1.9

check_for_app autoconf
check_for_app autoheader
check_for_app automake
check_for_app aclocal

gen_configure() {
	echo "Generating the configure script for $1 ..."
	shift

	aclocal -I "$@"
	autoconf
	autoheader
	automake --add-missing --copy 2>/dev/null
}

gen_configure "Asterisk" autoconf `find third-party -path '*/*/*' -prune -o -type d -print | xargs -I {} echo -I {}`
cd menuselect
gen_configure "menuselect" ../autoconf

exit 0
