#!/bin/sh

check_for_app() {
	$1 --version 2>&1 >/dev/null
	if [ $? != 0 ]
	then
		echo "Please install $1 and run bootstrap.sh again!"
		exit 1
	fi
}

uname -s | grep -q FreeBSD
if [ $? = 0 ]
then
	check_for_app autoconf259
	check_for_app autoheader259
	check_for_app automake19
	check_for_app aclocal19
	echo "Generating the configure script ..."
	aclocal19 2>/dev/null
	autoconf259
	autoheader259
	automake19 --add-missing --copy 2>/dev/null
else
	AUTOCONF_VERSION=2.59
	AUTOMAKE_VERSION=1.9
	export AUTOCONF_VERSION
	export AUTOMAKE_VERSION

	check_for_app autoconf
	check_for_app autoheader
	check_for_app automake
	check_for_app aclocal
	echo "Generating the configure script ..."
	aclocal 2>/dev/null
	autoconf
	autoheader
	automake --add-missing --copy 2>/dev/null
fi

exit 0
