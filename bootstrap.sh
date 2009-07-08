#!/bin/sh

check_for_app() {
	$1 --version 2>&1 >/dev/null
	if [ $? != 0 ]
	then
		echo "Please install $1 and run bootstrap.sh again!"
		exit 1
	fi
}

# On FreeBSD and OpenBSD, multiple autoconf/automake versions have different names.
# On linux, envitonment variables tell which one to use.

uname -s | grep -q BSD
if [ $? = 0 ] ; then	# BSD case
	case `uname -sr` in
		'FreeBSD 4'*)	# FreeBSD 4.x has a different naming
			MY_AC_VER=259
			MY_AM_VER=19
			;;
		*)
			MY_AC_VER=-2.62
			MY_AM_VER=-1.9
			;;
	esac
else	# linux case
	MY_AC_VER=
	MY_AM_VER=
	AUTOCONF_VERSION=2.60
	AUTOMAKE_VERSION=1.9
	export AUTOCONF_VERSION
	export AUTOMAKE_VERSION
fi

check_for_app autoconf${MY_AC_VER}
check_for_app autoheader${MY_AC_VER}
check_for_app automake${MY_AM_VER}
check_for_app aclocal${MY_AM_VER}

echo "Generating the configure script ..."

aclocal${MY_AM_VER} -I autoconf
autoconf${MY_AC_VER}
autoheader${MY_AC_VER}
automake${MY_AM_VER} --add-missing --copy 2>/dev/null

exit 0
