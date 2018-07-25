#!/usr/bin/env bash
CIDIR=$(dirname $(readlink -fn $0))
CLEANUP_DB=0
source $CIDIR/ci.functions

cp test-config.orig.yaml test-config.yaml
if [ $CLEANUP_DB -gt 0 ] ; then
	sudo -u postgres dropdb -e asterisk_test >/dev/null 2>&1 || :
	sudo -u postgres dropuser -e asterisk_test  >/dev/null 2>&1 || :
	sudo odbcinst -u -d -n "PostgreSQL-Asterisk-Test"
	sudo odbcinst -u -s -l -n "asterisk-connector-test"
fi
