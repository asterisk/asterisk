#!/usr/bin/env bash
CIDIR=$(dirname $(readlink -fn $0))
REALTIME=0
TEST_TIMEOUT=600
source $CIDIR/ci.functions
ASTETCDIR=$DESTDIR/etc/asterisk

if [ x"$WORK_DIR" != x ] ; then
	export AST_WORK_DIR="$(readlink -f $WORK_DIR)"
	mkdir -p "$AST_WORK_DIR"
fi

pushd $TESTSUITE_DIR

./cleanup-test-remnants.sh

if [ $REALTIME -eq 1 ] ; then
	$CIDIR/setupRealtime.sh --initialize-db=${INITIALIZE_DB:?0}
fi

export PYTHONPATH=./lib/python/
echo "Running tests ${TESTSUITE_COMMAND} ${AST_WORK_DIR:+with work directory ${AST_WORK_DIR}}"
./runtests.py --cleanup --timeout=${TEST_TIMEOUT} ${TESTSUITE_COMMAND} | contrib/scripts/pretty_print --no-color --no-timer --term-width=120 --show-errors || :

if [ $REALTIME -eq 1 ] ; then
	$CIDIR/teardownRealtime.sh --cleanup-db=${CLEANUP_DB:?0}
fi

if [ -f core* ] ; then
	echo "*** Found a core file after running unit tests ***"
	/var/lib/asterisk/scripts/ast_coredumper --no-default-search core*
	exit 1
fi

popd
