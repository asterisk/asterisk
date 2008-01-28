#!/bin/sh
# MiniMIME test cases

[ ! -x ./tests/parse -o ! -x ./tests/create ] && {
	echo "You need to compile the test suite first to accomplish tests"
	exit 1
}

LD_LIBRARY_PATH=${PWD}
export LD_LIBRARY_PATH

DIRECTORY=${1:-tests/messages}
FILES=${2:-"*"}

TESTS=0
F_ERRORS=0
F_INVALID=""
M_ERRORS=0
M_INVALID=""
for f in ${DIRECTORY}/${FILES}; do
	if [ -f "${f}" ]; then
		TESTS=$((TESTS + 2))
		echo -n "Running PARSER test for $f (file)... "
		output=`./tests/parse $f 2>&1`
		[ $? != 0 ] && {
			echo "FAILED ($output)"
			F_ERRORS=$((F_ERRORS + 1))
			F_INVALID="${F_INVALID} ${f} "
		} || {
			echo "PASSED"
		}
		echo -n "Running PARSER test for $f (memory)... "
		output=`./tests/parse -m $f 2>&1`
		[ $? != 0 ] && {
			echo "FAILED ($output)"
			M_ERRORS=$((M_ERRORS + 1))
			M_INVALID="${M_INVALID} ${f} "
		} || {
			echo "PASSED"
		}
	fi
done

echo "Ran a total of ${TESTS} tests"

if [ ${F_ERRORS} -gt 0 ]; then
	echo "!! ${F_ERRORS} messages had errors in file based parsing"
	echo "-> ${F_INVALID}"
fi	
if [ ${M_ERRORS} -gt 0 ]; then
	echo "!! ${F_ERRORS} messages had errors in memory based parsing"
fi	

unset LD_LIBRARY_PATH
