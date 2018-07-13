#!/usr/bin/env bash
CIDIR=$(dirname $(readlink -fn $0))
source $CIDIR/ci.functions
ASTETCDIR=$DESTDIR/etc/asterisk

pushd $TESTSUITE_DIR

./cleanup-test-remnants.sh
export PYTHONPATH=./lib/python/
echo "Running tests ${TEST_COMMAND}"
./runtests.py --cleanup ${TEST_COMMAND} | contrib/scripts/pretty_print --no-color --no-timer --term-width=120 --show-errors || :

if [ -f core* ] ; then
	echo "*** Found a core file after running unit tests ***"
	/var/lib/asterisk/scripts/ast_coredumper --no-default-search core*
	exit 1
fi

popd