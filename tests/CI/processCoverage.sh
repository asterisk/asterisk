#!/usr/bin/env bash

CIDIR=$(dirname $(readlink -fn $0))
source $CIDIR/ci.functions

if [ ! -r main/asterisk.gcno ]; then
	# Coverage is not enabled.
	exit 0
fi

if [ -z $LCOV_DIR ]; then
	LCOV_DIR="${OUTPUT_DIR:+${OUTPUT_DIR}/}lcov"
fi

if [ -z $COVERAGE_DIR ]; then
	COVERAGE_DIR="${OUTPUT_DIR:+${OUTPUT_DIR}/}coverage"
fi

if [ -z $ASTERISK_VERSION ]; then
	ASTERISK_VERSION=$(./build_tools/make_version .)
fi

set -x
# Capture counter data from testing
lcov --no-external --capture --directory . --output-file ${LCOV_DIR}/tested.info > /dev/null

# Combine initial and tested data.
lcov \
	--add-tracefile ${LCOV_DIR}/initial.info \
	--add-tracefile ${LCOV_DIR}/tested.info \
	--output-file ${LCOV_DIR}/combined.info > /dev/null

# We don't care about coverage reporting for tests, utils or third-party.
lcov --remove ${LCOV_DIR}/combined.info \
		"${PWD}/main/dns_test.*" \
		"${PWD}/main/test.*" \
		"${PWD}/tests/*" \
		"${PWD}/utils/*" \
		"${PWD}/third-party/*" \
	--output-file ${LCOV_DIR}/filtered.info > /dev/null

# Generate HTML coverage report.
mkdir -p ${COVERAGE_DIR}
genhtml --prefix ${PWD} --ignore-errors source ${LCOV_DIR}/filtered.info \
	--legend --title "Asterisk ${ASTERISK_VERSION}" --output-directory=${COVERAGE_DIR} > /dev/null
